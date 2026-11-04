# What a virtual USB device is useful for

Vdevice runs on the Windows PC. Your application supplies descriptors and reports
and keeps a virtual device alive. It does not modify a physical keyboard, flash
an Arduino, or automatically translate serial messages from a board.

## Two small developer workflows

- **Repeatable application input:** supply a known press/release sequence to a
  virtual keyboard or controller while testing an application. Keep an explicit
  neutral/release state and check what the receiving application actually saw.
- **Descriptor/report experiments:** validate a candidate description offline,
  then create the virtual device and inspect host enumeration, output and control
  requests. A changed VID/PID does not implement a device's missing protocol.

These workflows still require the receiver, Windows configuration and device
protocol to be tested. An offline preview or successful enumeration alone does
not prove desktop typing, game acceptance or physical-hardware equivalence.

## Arduino or another microcontroller

For a keyboard that plugs into other PCs, a USB-capable board with native HID
firmware is often the direct route. Follow that board's documented capabilities.
Vdevice is an alternative Windows-side component when your design intentionally
needs software-generated devices on the PC.

A possible custom design is `board button -> serial message -> your Windows
program -> Vdevice report`. The serial bridge is not included. It would need
framing, disconnect/timeout handling and neutral reports so a lost connection
does not leave a key logically held. No particular board has been validated by
this explanation.

## A keyboard is detected but produces no text

1. Validate the report descriptor and match the report layout, endpoint and any
   report ID to it. Do not add a report ID to a no-ID layout.
2. Service host output/control requests and wait for the documented ready/configured
   state. Check every SDK error instead of treating creation as final readiness.
3. On an authorized test setup, send a press and a neutral/release report, focus
   the intended receiver and observe its actual result. The keyboard UI example
   documents a delayed tap to allow focus switching.
4. Check [validation and protocol limits](VALIDATION.md) before claiming support.

A separate UMDF HID implementation must be diagnosed in its own architecture.
Vdevice's KMDF/UdeCx example is not a demonstrated fix for every UMDF keyboard.

Live use requires the matching installed driver and administrator access. Public
user-mode source is MIT; kernel source is private. The public driver has a one-hour
per-kernel-boot evaluation; unrestricted use has separately agreed commercial terms.

[Raw API/lifecycle](API.md) · [control panels](../Examples/UI/README.md)
