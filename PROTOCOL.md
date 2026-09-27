# Abriblue / A.S. Pool "Cover" BLE protocol

Reverse-engineered from Aero XP 2.4.7 (`com.aspool.app.aero`).

## Transport

- Advertised name: `Cover`
- Microchip transparent UART service (RN4870-style):
  - Service `49535343-fe7d-4ae5-8fa9-9fafd205e455`
  - Write (app → cover) `49535343-8841-43f4-a8d4-ecbe34729bb3`
  - Notify (cover → app) `49535343-1e4d-4bd9-ba61-23c647249616`
- App requests MTU 120. No BLE bonding or encryption.

## Frame (both directions)

```
0x02 | key (4 ASCII digits) | mode (1 byte) | payload (ASCII) | 0x03 | xor
xor = 0xFF ^ every preceding byte
```

- `key` is the 4-digit code typed in the app when adding the cover. It is the only auth.
- Mode `t` = command. The other modes (lights, OTA, logs, etc.) are not needed here.

## Command payloads (mode `t`)

| Payload | Meaning |
|---|---|
| `7` | Keep-alive ("boîtier allumé"). The app sends it every 150ms while connected. |
| `1` / `2` / `3` | Open (BP1): ON / hold (BIS) / OFF |
| `4` / `5` / `6` | Close (BP2): ON / hold (BIS) / OFF |
| `3` | Stop |
| `8` | Request parameters. The reply is a `P` notification. |

The app never sends `3` as a stop: a hold-direction move stops when the button is released. On a real cover, neither releasing (`OFF`) nor a second tap of the same button stops a tap-direction move; the bridge taps the opposite button instead (under test).

The fitter-only variants (`a`–`l`) bypass safeties. Never use them.

Press sequence: `ON` once, then `BIS` every 50ms while the button is held, then `OFF` on release. Frames go out through the 150ms send loop, and a repeated identical action is sent at most every 130ms (4000ms on "universal" cards).

## Notifications

Byte 5 of the reply (after `0x02` and the key) is the type:

- `P` = parameters
- `E` = wrong key
- `V` = alert

`P` layout (by character index): operating mode at `[8]`, card type at `[24..26)`, brand at `[26]`, cover state at `[29]`.

- Operating mode:
  - `1` = standard: open is one tap, close is hold-to-run
  - `2` = impulse: both are one tap
  - `3` = hold: both are hold-to-run
- Cover state (Immeo cards only):
  - `1` = OK
  - `0` = not initialised
  - `2`–`9` = faults
- Immeo cards also report position and motor rotation (`0` = off, `1` = opening, `2` = closing) in fields `[15]`–`[23]`. The exact offsets still need verifying against a live capture.
