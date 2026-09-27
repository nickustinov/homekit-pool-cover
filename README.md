# homekit-pool-cover

An ESP32 firmware that lets you open, close and stop an Abriblue (A.S. Pool) slatted pool cover from the Apple Home app. The ESP32 connects to the cover's controller over Bluetooth, the same way the Aero XP app does, and shows up in HomeKit as a native accessory. Homebridge is not needed.

```
Home app ──HomeKit (WiFi)──▶ ESP32 ──Bluetooth──▶ cover controller
```

> **Safety:** in the controller's default mode, closing is hold-to-run on purpose, so nobody closes the cover on someone in the pool. This bridge holds the button for you. Only operate the cover when you can see the pool.

## What you get in the Home app

One accessory called **Pool Cover** with:

- **Window covering** – open, close, or drag to a position.
- **Stop** – a switch that stops the cover mid-move and turns itself back off after a second.

The controller doesn't report where the cover is, so position is estimated from the elapsed time. Set your real travel times (see [Configure](#configure)) to make the estimate match.

## Hardware

- An ESP32 board. This was built for the ESP32-A1S audio kit, but any classic ESP32 dev board works. The audio parts of the A1S aren't used.
- A USB **data** cable. Many cables only charge.
- A place within Bluetooth range of the cover controller, with WiFi coverage.

## Install the tools (macOS)

```sh
brew install platformio
```

The first build downloads the ESP32 toolchain, which is a few hundred MB.

## Configure

```sh
cp src/config.example.h src/config.h
```

Then edit `src/config.h`:

| Setting | What to put there |
|---|---|
| `COVER_KEY` | The 4-digit code you entered in the Aero XP app when you added the cover. |
| `OPEN_TIME_MS` | How long a full open takes, in milliseconds. Time it with a stopwatch. |
| `CLOSE_TIME_MS` | How long a full close takes, in milliseconds. |
| `EXTRA_HOLD_MS` | How long to keep holding after the estimated end, so the cover always reaches its end stop. 5000 is fine. |

`src/config.h` is git-ignored because it holds your cover code.

## Flash

Plug the ESP32 into your Mac, then in **Terminal**:

```sh
cd path/to/abri
pio run -t upload -t monitor
```

This builds the firmware, flashes it, and opens the **serial monitor**: a live text connection to the ESP32 over USB, where you can see its log and type commands.

If the upload can't find the board, run `ls /dev/cu.*` and look for `/dev/cu.usbserial-…` or `/dev/cu.SLAB_USBtoUART`. If nothing shows up, try another cable or install the USB driver for your board (CP210x or CH340).

## Connect to WiFi

In the serial monitor:

1. Type `W` and press Enter.
2. Pick your network from the list and enter the password.
3. The ESP32 restarts and connects. You'll see its IP address in the log.

Press `Ctrl+C` to leave the monitor. The ESP32 keeps running. To open the monitor again later without reflashing, run `pio device monitor`.

## Add to the Home app

1. In the Home app, tap **+ → Add Accessory → More options…**
2. Pick **Pool Cover** and enter the setup code `466-37-726`.

That is HomeSpan's default code. To set your own, type `S 12345678` (any 8 digits) in the serial monitor before pairing.

## First test

Keep the serial monitor open and the pool in sight:

1. Tap open in the Home app. The log should show `[cover] found at …` and then `[cover] opening`.
2. Tap **Stop** partway and check that the cover stops.
3. Do a full close.

| Log message | Meaning |
|---|---|
| `[cover] not found` | The ESP32 can't see the controller. Move it closer, and close the Aero XP app on your phone (the controller accepts one connection at a time). |
| `[cover] connect failed` | Same as above. Also try power-cycling the ESP32. |
| `no parameters reply … wrong key?` | The controller rejected `COVER_KEY`. Check the code. |

## Useful serial commands

HomeSpan has a built-in command line. Type `?` in the monitor for the full list. The most useful ones:

| Command | What it does |
|---|---|
| `W` | Set the WiFi network |
| `S 12345678` | Set the HomeKit setup code |
| `U` | Unpair from HomeKit (remove it in the Home app too) |
| `E` | Erase all settings: WiFi, pairing and saved position |

## Troubleshooting

- **The position in the Home app drifts.** Re-time a full open and close and update `OPEN_TIME_MS` and `CLOSE_TIME_MS`. A full open or close always runs to the end stop and resets the estimate.
- **A neighbour's cover gets picked up.** The ESP32 connects to the first device named `Cover` it finds. Lock it to your controller's address, which the log prints as `[cover] found at …`, in `findCover()` in `src/main.cpp`.
- **The first `pio run` fails with a GitHub timeout.** PlatformIO's downloader sometimes times out when curl works fine. Download the platform manually and install it:
  ```sh
  curl -L -o /tmp/pf.zip https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip
  pio pkg install -g -p file:///tmp/pf.zip
  pio run
  ```

## Development

- `src/main.cpp` – the firmware: Bluetooth task and HomeKit services
- `src/frame.h` – builds the messages sent to the controller
- `src/config.h` – your settings (template in `src/config.example.h`)
- `PROTOCOL.md` – the controller's Bluetooth protocol, reverse-engineered from Aero XP 2.4.7

Run the frame test on your Mac:

```sh
g++ -std=c++17 -I src test/test_frame.cpp -o /tmp/test_frame && /tmp/test_frame
```

Not affiliated with or endorsed by Abriblue or A.S. Pool.

Built with [HomeSpan](https://github.com/HomeSpan/HomeSpan) and [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino).
