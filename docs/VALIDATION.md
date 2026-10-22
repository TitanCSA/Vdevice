# Compatibility and validation

The package uses the supplied Microsoft-signed `Vdevice.sys` 1.0.0.0, without kernel
modifications. The SYS and INF passed SignTool catalog verification under kernel
signing policy; both installers contain byte-identical driver files.

## Checks performed

- A clean public-source build compiled the SDK, five native control panels,
  Descriptor Workshop, and both installers with warnings
  treated as errors. No kernel source or WDK was needed for that build.
- Native offline tests cover **over 350,000** protocol/range,
  packing, touch-coordinate, GIP-handshake and malformed-packet checks.
- **15** managed installer/XAML suites, including **56** installation-state,
  real-view, restart, cleanup and process-output assertions, and **25,371** workshop serializer/native
  validation checks passed without driver installation or device creation.
- Installer 1.0.1.0 passed install, reinstall, uninstall, service cleanup and
  simultaneous UI/console exclusion checks in an isolated Windows 11 guest.
  A pending-restart fixture persisted across status reads and cleared after a
  normal Windows restart. This did not require a kernel rebuild or signing change.
- The signed package was installed and its loaded SYS hash verified in isolated
  Hyper-V Windows 10 build 19045 and Windows 11 build 26200 guests.
- All five virtual-device examples passed repeated live runs. Windows 11 ran
  with standard Driver Verifier selected for `Vdevice.sys`; Windows 10 ran its
  final campaign without Driver Verifier.

| Live test | Verified behavior |
| --- | --- |
| Keyboard / mouse | Windows configured the device and consumed the USB input reports. Desktop typing/cursor movement was not asserted by the automated test. |
| Xbox 360 | Exact buttons, four axes and both triggers via real `XInputGetState`; rumble returned through host OUT; 2,000 state updates and release per run. |
| DS4 | Exact buttons/hat/sticks, gyro and touch coordinates via a Windows HID handle; feature and rumble/RGB output; 2,000 updates per run. |
| Xbox One GIP | Hello, fragmented metadata/ACKs, USB security opt-out acceptance, exact real XInput state, host motor commands, 2,000 updates and release per run. |
| Native control panels | All five panels created a live device, sent a state, reset it, disconnected and exited normally in Windows 11; offline preview opening/closing and rendering also passed. Their protocol code is also used by the automated tests. |

GIP briefly repeats the latest state during initial Windows consumer creation
and after updates. The first send remains immediate, and repetition stops when
idle. This covers the distinction between a configured USB protocol and an
application consumer that has finished attaching.

## Device scope

The five examples implement PC input protocols. They do not recreate a complete
retail device's firmware, console authentication, Bluetooth stack, audio or
accessory interfaces. VID/PID values alone do not implement those features.

Xbox 360 uses ViGEm-derived descriptors/report framing with the gamepad USB
function exposed. Its unused voice/security interfaces are omitted: publishing
the full interface set without implementing it prevented initialization in a
Windows 11 experiment. DS4 uses ViGEm's 467-byte HID report descriptor and USB
interface; its identity is synthetic and calibration bytes are example data.

GIP uses standard Xbox One gamepad messages and the Windows USB opt-out GUID
documented in **MS-GIPUSB section 5.1**. It uses development VID/PID `FFFF:0004`;
it is not an authenticated Microsoft controller and does not work as an
authenticated Xbox console accessory. Use assigned identities for distribution.
Guide messages are encoded, but their application/UI effects were not verified.

The bus supports up to 16 devices. Legacy XInput exposes at most four controller
slots; that API limit does not become 16 just because the bus supports 16 USB
devices. USB low/full/high speed, one configuration, alternate setting zero,
interrupt/bulk and bounded transfers are supported. SuperSpeed, isochronous
audio, external hubs, additional configurations and alternate settings are not.

## Observed platform issue

An early Windows 10 run with an inherited Driver Verifier configuration selected
for another project produced bugcheck `0xC9/7` during Xbox 360 removal. The dump
identified Microsoft's `xusb22!XInputControllerDevice::cleanupWaitingRequests`
completing an IRP with its cancel routine still set. The final Windows 10 runs
passed after that inherited lab debugging configuration was reset. This is not
a claim that every Windows/Verifier configuration is crash-free; retain this
limitation when reproducing the tests.

The shared two-hour boot allowance and its teardown were previously exercised
in the kernel validation campaign. These new example tests do not claim a fresh
two-hour wall-clock expiry run, a console certification, every game/API behavior,
or a measured physical input latency.
