#include <HomeSpan.h>
#include <NimBLEDevice.h>
#include "config.h"
#include "frame.h"

static const NimBLEUUID SERVICE_UUID("49535343-fe7d-4ae5-8fa9-9fafd205e455");
static const NimBLEUUID WRITE_UUID("49535343-8841-43f4-a8d4-ecbe34729bb3");
static const NimBLEUUID NOTIFY_UUID("49535343-1e4d-4bd9-ba61-23c647249616");
static const uint32_t SEND_INTERVAL_MS = 150;  // same cadence as the app's send loop

enum Dir { NONE, OPEN, CLOSE };

// Shared between HomeKit (loop task) and the BLE task
static volatile Dir request = NONE;       // set by HomeKit, taken by the BLE task
static volatile bool busy = false;        // a move is in progress (connecting or holding)
static volatile bool stopRequested = false;
static volatile uint32_t holdStart = 0;   // millis() when the button went down, 0 when not held
static volatile bool moveFailed = false;
static volatile char replyType = 0;       // type byte of the last notification from the cover

static NimBLEClient *client;
static NimBLEAddress coverAddr;
static bool coverFound = false;

// ---------- BLE side ----------

static void onNotify(NimBLERemoteCharacteristic *, uint8_t *data, size_t len, bool) {
  if (len > 5 && data[0] == 0x02) replyType = data[5];
}

static bool send(NimBLERemoteCharacteristic *c, char action) {
  uint8_t buf[16];
  size_t n = buildCommand(COVER_KEY, action, buf);
  return c->writeValue(buf, n, true);
}

// ponytail: takes the first device named "Cover"; pin its MAC here if a neighbour has one too
static bool findCover() {
  if (coverFound) return true;
  NimBLEScanResults res = NimBLEDevice::getScan()->getResults(10000, false);
  for (int i = 0; i < res.getCount(); i++) {
    const NimBLEAdvertisedDevice *d = res.getDevice(i);
    if (d->getName() == "Cover") {
      coverAddr = d->getAddress();
      coverFound = true;
      Serial.printf("[cover] found at %s\n", coverAddr.toString().c_str());
    }
  }
  return coverFound;
}

// Connects, checks the key, then holds the button like the app does:
// ON once, HOLD every 150ms, OFF on release (plus STOP when stopped early).
static bool runMove(Dir dir, uint32_t holdMs) {
  if (!findCover()) {
    Serial.println("[cover] not found");
    return false;
  }
  if (!client->connect(coverAddr)) {
    Serial.println("[cover] connect failed");
    return false;
  }
  NimBLERemoteCharacteristic *tx = nullptr, *rx = nullptr;
  if (NimBLERemoteService *svc = client->getService(SERVICE_UUID)) {
    tx = svc->getCharacteristic(WRITE_UUID);
    rx = svc->getCharacteristic(NOTIFY_UUID);
  }
  bool ok = tx && rx && rx->subscribe(true, onNotify);

  if (ok) {  // the app asks for parameters first; 'P' means the key was accepted
    replyType = 0;
    send(tx, GET_PARAMS);
    for (int i = 0; i < 50 && !replyType && client->isConnected(); i++) delay(100);
    ok = replyType == 'P';
    if (!ok) Serial.printf("[cover] no parameters reply (got '%c'), wrong key?\n", replyType ? replyType : '-');
  }

  if (ok && !stopRequested) {
    const char on = dir == OPEN ? OPEN_ON : CLOSE_ON;
    const char hold = dir == OPEN ? OPEN_HOLD : CLOSE_HOLD;
    const char off = dir == OPEN ? OPEN_OFF : CLOSE_OFF;
    ok = send(tx, on);
    holdStart = millis();
    while (ok && !stopRequested && millis() - holdStart < holdMs && client->isConnected()) {
      delay(SEND_INTERVAL_MS);
      ok = send(tx, hold);
    }
    send(tx, off);
    if (stopRequested) send(tx, STOP);
    holdStart = 0;
  }
  client->disconnect();
  return ok;
}

static void bleTask(void *) {
  for (;;) {
    Dir dir = request;
    if (dir == NONE) {
      delay(50);
      continue;
    }
    request = NONE;
    stopRequested = false;
    Serial.printf("[cover] %s\n", dir == OPEN ? "opening" : "closing");
    moveFailed = !runMove(dir, (dir == OPEN ? OPEN_TIME_MS : CLOSE_TIME_MS) + EXTRA_HOLD_MS);
    busy = false;
  }
}

// ---------- HomeKit side ----------

// PositionState values
static const int CLOSING = 0, OPENING = 1, STOPPED = 2;

// ponytail: position is estimated from elapsed time; Immeo cards report real position (PROTOCOL.md) if drift matters
struct PoolCover : Service::WindowCovering {
  SpanCharacteristic *current, *target, *state;
  Dir dir = NONE;  // direction of the move we started, NONE when idle
  int startPos = 0;

  PoolCover() : Service::WindowCovering() {
    current = new Characteristic::CurrentPosition(0, true);  // true = persisted across reboots
    target = new Characteristic::TargetPosition(0, true);
    state = new Characteristic::PositionState(STOPPED);
    target->setVal(current->getVal());  // never resume a move after a reboot
  }

  int estimate() {
    if (!holdStart) return startPos;
    uint32_t elapsed = millis() - holdStart;
    int pos = dir == OPEN ? startPos + (int)(elapsed * 100 / OPEN_TIME_MS)
                          : startPos - (int)(elapsed * 100 / CLOSE_TIME_MS);
    return constrain(pos, 0, 100);
  }

  void loop() override {
    int cur = current->getVal(), tgt = target->getVal();

    if (dir == NONE) {  // idle: start a move if HomeKit wants a different position
      if (tgt == cur) return;
      dir = tgt > cur ? OPEN : CLOSE;
      startPos = cur;
      busy = true;
      request = dir;
      state->setVal(dir == OPEN ? OPENING : CLOSING);
      return;
    }

    int pos = estimate();
    if (busy) {  // moving: track position, stop at an intermediate target or on reversal
      if (pos != cur) current->setVal(pos);
      bool reachedPartial = (dir == OPEN && tgt < 100 && pos >= tgt) || (dir == CLOSE && tgt > 0 && pos <= tgt);
      bool reversed = (dir == OPEN && tgt < pos) || (dir == CLOSE && tgt > pos);
      if (reachedPartial || reversed) stopRequested = true;
      return;
    }

    // move finished
    if (moveFailed) {
      current->setVal(pos);
      target->setVal(pos);
    } else if (!stopRequested) {
      current->setVal(dir == OPEN ? 100 : 0);  // held to the end stop
    } else if (abs(tgt - pos) <= 5) {
      current->setVal(tgt);  // stopped where asked; snap to avoid a tiny corrective move
    } else {
      current->setVal(pos);  // reversed: next loop() starts the other way
    }
    state->setVal(STOPPED);
    dir = NONE;
  }
};

struct StopButton : Service::Switch {
  SpanCharacteristic *on;
  PoolCover *cover;

  StopButton(PoolCover *cover) : Service::Switch(), cover(cover) {
    on = new Characteristic::On(false);
    new Characteristic::ConfiguredName("Stop");
  }

  boolean update() override {
    if (on->getNewVal()) {
      stopRequested = true;
      cover->target->setVal(cover->current->getVal());
    }
    return true;
  }

  void loop() override {
    if (on->getVal() && on->timeVal() > 1000) on->setVal(false);  // momentary button
  }
};

void setup() {
  Serial.begin(115200);

  NimBLEDevice::init("");
  NimBLEDevice::setMTU(120);  // same as the app
  client = NimBLEDevice::createClient();
  xTaskCreatePinnedToCore(bleTask, "cover-ble", 8192, nullptr, 1, nullptr, 0);

  homeSpan.begin(Category::WindowCoverings, "Pool Cover");
  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  PoolCover *cover = new PoolCover();
  new StopButton(cover);
}

void loop() {
  homeSpan.poll();
}
