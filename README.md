# homekit-pool-cover

An ESP32 firmware that lets you open and close an Abriblue (A.S. Pool) slatted pool cover from the Apple Home app. The ESP32 connects to the cover's controller over Bluetooth, the same way the Aero XP app does, and shows up in HomeKit as a native accessory. Homebridge is not needed.

```
Home app ──HomeKit (WiFi)──▶ ESP32 ──Bluetooth──▶ cover controller
```

> **Safety:** in the controller's default mode, closing is hold-to-run on purpose, so nobody closes the cover on someone in the pool. This bridge holds the button for you. Only operate the cover when you can see the pool.

## What you get in the Home app

One accessory called **Pool Cover** with:

- **Open** – starts opening. It stays on while the cover moves; turn it off to stop.
- **Close** – same, for closing.

The Home app may group the two into one tile. To get separate buttons, open the tile's settings and choose **Show as Separate Tiles**.

HomeKit setup code: **`466-37-726`** (HomeSpan's default, see [Add to the Home app](#add-to-the-home-app) to change it).

### How moves and stops work

The controller has three operating modes, and the bridge reads the current one each time it connects:

| Mode | Open | Close |
|---|---|---|
| Standard (default) | tap | hold-to-run |
| Impulse | tap | tap |
| Hold | hold-to-run | hold-to-run |

- **Hold-to-run direction:** the bridge keeps the button held for up to `MOVE_TIME_MS`. Turning the switch off releases it and the cover stops.
- **Tap direction:** the bridge taps once and stays connected for `MOVE_TIME_MS`. Turning the switch off presses the opposite button for `STOP_PRESS_MS`, which stops the cover.

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
| `MOVE_TIME_MS` | How long a move lasts, in milliseconds. Set it a few seconds longer than your slowest full open or close. The controller stops the motor at its end stops by itself. |
| `STOP_PRESS_MS` | How long the opposite button is pressed to stop an opening cover. If turning Open off doesn't stop it, increase this. If the cover starts closing afterwards, decrease it. |

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

1. Turn on **Open** in the Home app. The log should show `[cover] opening`, then `[cover] mode '1', tapping open`.
2. Turn **Open** off partway and check that the cover stops.
3. Turn on **Close**, turn it off partway, and check that it stops too.
4. Turn on **Close** again and let it run to the end.

| Log message | Meaning |
|---|---|
| `[cover] not found` | The ESP32 can't see the controller. Move it closer, and close the Aero XP app on your phone (the controller accepts one connection at a time). |
| `[cover] connect attempt … failed` | The controller was busy. The bridge retries 3 times, 2 seconds apart. If all fail, check as for "not found". |
| `no parameters reply … wrong key?` | The controller rejected `COVER_KEY`. Check the code. |

## Useful serial commands

HomeSpan has a built-in command line. Type `?` in the monitor for the full list. The most useful ones:

| Command | What it does |
|---|---|
| `W` | Set the WiFi network |
| `S 12345678` | Set the HomeKit setup code |
| `U` | Unpair from HomeKit (remove it in the Home app too) |
| `E` | Erase all settings: WiFi and pairing |

## Troubleshooting

- **Close stops before the end.** A full close takes longer than `MOVE_TIME_MS`. Increase it and reflash.
- **The Home app shows old controls after an update.** Remove the accessory in the Home app, type `U` in the serial monitor, and pair again.
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
