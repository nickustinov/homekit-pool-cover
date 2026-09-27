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
static volatile Dir request = NONE;  // set by HomeKit, taken by the BLE task
static volatile Dir active = NONE;   // direction of the move in progress
static volatile bool stopRequested = false;
static volatile char replyType = 0;  // type byte of the last notification from the cover
static volatile char opMode = 0;     // operating mode from the 'P' reply, see PROTOCOL.md

static NimBLEClient *client;
static NimBLEAddress coverAddr;
static bool coverFound = false;

// ---------- BLE side ----------

static void onNotify(NimBLERemoteCharacteristic *, uint8_t *data, size_t len, bool) {
  if (len > 5 && data[0] == 0x02) replyType = data[5];
  if (len > 8 && data[5] == 'P') opMode = data[8];
}

static bool send(NimBLERemoteCharacteristic *c, char action) {
  uint8_t buf[16];
  size_t n = buildCommand(COVER_KEY, action, buf);
  return c->writeValue(buf, n, true);
}

// Short press, like a finger tap in the app: ON, HOLD, OFF
static bool tap(NimBLERemoteCharacteristic *c, char on, char hold, char off) {
  bool ok = send(c, on);
  delay(SEND_INTERVAL_MS);
  ok = ok && send(c, hold);
  delay(SEND_INTERVAL_MS);
  return send(c, off) && ok;
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

// The controller refuses connections for a moment after the previous one closes
static bool connect() {
  for (int attempt = 1; attempt <= 3; attempt++) {
    if (client->connect(coverAddr)) return true;
    Serial.printf("[cover] connect attempt %d failed\n", attempt);
    delay(2000);
  }
  return false;
}

// Mode '1' (standard): open is a tap, close is hold-to-run. '2': both taps. '3': both hold.
static bool isTapDirection(Dir dir) {
  return opMode == '2' || (opMode != '3' && dir == OPEN);
}

// Runs one move while staying connected for MOVE_TIME_MS so a stop takes effect at once.
// Hold directions keep the button down and stop on release. Tap directions start with
// a tap and stop with a second tap, like the key switch.
static bool runMove(Dir dir) {
  if (!findCover()) {
    Serial.println("[cover] not found");
    return false;
  }
  if (!connect()) return false;

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
    const bool tapMode = isTapDirection(dir);
    Serial.printf("[cover] mode '%c', %s %s\n", opMode ? opMode : '?', tapMode ? "tapping" : "holding",
                  dir == OPEN ? "open" : "close");

    ok = tapMode ? tap(tx, on, hold, off) : send(tx, on);
    uint32_t start = millis();
    while (ok && !stopRequested && millis() - start < MOVE_TIME_MS && client->isConnected()) {
      delay(SEND_INTERVAL_MS);
      ok = send(tx, tapMode ? KEEP_ALIVE : hold);
    }
    if (!tapMode) send(tx, off);
    else if (stopRequested) tap(tx, on, hold, off);
    if (stopRequested) Serial.println("[cover] stopped");
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
    active = dir;
    request = NONE;
    stopRequested = false;
    Serial.printf("[cover] %s\n", dir == OPEN ? "opening" : "closing");
    if (!runMove(dir)) Serial.println("[cover] move failed");
    active = NONE;
    delay(1000);  // let the controller free its connection before the next move
  }
}

// ---------- HomeKit side ----------

// Open / Close: turning on starts the move, the switch stays on while it runs,
// turning it off stops it. A move in the other direction is stopped first.
struct MoveButton : Service::Switch {
  SpanCharacteristic *on;
  Dir dir;

  MoveButton(Dir dir, const char *name) : Service::Switch(), dir(dir) {
    on = new Characteristic::On(false);
    new Characteristic::ConfiguredName(name);
  }

  boolean update() override {
    if (on->getNewVal()) {
      if (active != NONE) stopRequested = true;
      request = dir;
    } else if (active == dir) {
      stopRequested = true;
    }
    return true;
  }

  void loop() override {
    bool running = request == dir || active == dir;
    if (on->getVal() != running && on->timeVal() > 1000) on->setVal(running);
  }
};

struct StopButton : Service::Switch {
  SpanCharacteristic *on;

  StopButton() : Service::Switch() {
    on = new Characteristic::On(false);
    new Characteristic::ConfiguredName("Stop");
  }

  boolean update() override {
    if (on->getNewVal()) {
      request = NONE;
      if (active != NONE) stopRequested = true;
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

  homeSpan.begin(Category::Switches, "Pool Cover");
  new SpanAccessory();
  new Service::AccessoryInformation();
  new Characteristic::Identify();
  new MoveButton(OPEN, "Open");
  new MoveButton(CLOSE, "Close");
  new StopButton();
}

void loop() {
  homeSpan.poll();
}
