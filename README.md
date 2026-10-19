# Vdevice — Windows Kernel Driver for Virtual USB Keyboards, Mice & Gamepads

[![Build](https://github.com/TitanCSA/Vdevice/actions/workflows/build.yml/badge.svg)](https://github.com/TitanCSA/Vdevice/actions/workflows/build.yml)
[![Release](https://img.shields.io/github/v/release/TitanCSA/Vdevice?include_prereleases)](https://github.com/TitanCSA/Vdevice/releases/latest)

Create and control virtual USB devices on **Windows 10/11 x64** with a Microsoft-signed kernel driver and a **two-file C++17 SDK**. Supply raw USB/HID descriptors, VID/PID, strings, endpoints and reports. The library has seven operations and no built-in device profiles.

**Demo: two hours per Windows kernel boot**, shared by all applications, starting at the first successful device creation. Expiry disconnects virtual devices and blocks new creation/transfers until **Windows Restart**. For the unrestricted version, **DM `titancsa` on Discord**.

| Keyboard control panel | DS4 control panel |
| --- | --- |
| ![Keyboard example in offline preview](docs/images/keyboard.png) | ![DualShock 4 example in offline preview](docs/images/ds4.png) |

The screenshots show the applications' offline preview; they do not represent a hardware test.

## Install and try

1. Download **[VdeviceSetup.exe](https://github.com/TitanCSA/Vdevice/releases/latest)** and run it. Accept UAC, then click **Install / update**. Restart if requested.
2. Extract **Vdevice-Examples-1.0.0.zip** from the same release.
3. Open a control panel, click **Create device**, accept elevation if needed, and wait for **READY**. Set fields, choose buttons, then click **Send state**. **Release / neutral** releases held inputs; **Disconnect** removes the device.

For a keyboard test in another application, choose `key1 = 4` (A), click **Tap state in 3 s**, then focus your editor. The state is released after 50 ms. Key usages follow the target application's keyboard layout.

| Console / UI executable | Device and controls |
| --- | --- |
| `Keyboard.exe` / `VdeviceKeyboardUI.exe` | USB boot keyboard; eight modifier bits and six keys; host LED feedback |
| `Mouse.exe` / `VdeviceMouseUI.exe` | Five buttons, signed 16-bit relative X/Y, vertical and horizontal wheels |
| `Xbox360.exe` / `VdeviceXbox360UI.exe` | ViGEm-style Xbox 360 **XInput** gamepad; four stick axes, triggers, buttons and host rumble |
| `DualShock4.exe` / `VdeviceDualShock4UI.exe` | ViGEm-style **DS4** USB interface; sticks, triggers, D-pad, buttons, gyro, accelerometer, two touch contacts and rumble/RGB feedback |
| `XboxOneGip.exe` / `VdeviceXboxOneGipUI.exe` | Native **Xbox One GIP** PC gamepad; Hello, metadata, Guide messages, analog controls and host motor commands |

The GIP example requires **Windows 10 21H2 or later**. It advertises Microsoft's documented USB security opt-out interface; it does not emulate console authentication or contain certificates/keys. Xbox 360 exposes the gamepad function, without ViGEm's unused voice/security interfaces. DS4 uses ViGEm's USB/report descriptors with synthetic identity and example calibration data. These are PC protocol implementations, not complete console firmware replicas. See [compatibility and validation](docs/VALIDATION.md).

## Console commands

Run one example from an **administrator terminal**. It checks installation and creates its device, then accepts commands until `quit` or end of input:

```text
Keyboard.exe
tap key1 4
set modifiers 2 key1 5
reset
quit
```

```text
Xbox360.exe
set buttons 0x1000 lx 16000 ly -8000 lt 128
reset
quit
```

`set FIELD VALUE [FIELD VALUE ...]` sends immediately; `tap FIELD VALUE [MS]` temporarily changes one field. `send`, `reset`, `fields`, `info`, `raw ENDPOINT HEX-BYTES`, `sleep MS` and `help` are also available. Use `--help` or `--validate` offline, without opening the driver. See [all console examples](ExamplesConsole/README.md).

## Raw C++ API

Copy [`Vdevice.h`](Vdevice/Vdevice.h) and [`Vdevice.cpp`](Vdevice/Vdevice.cpp) into your Windows C++17 project. Every device uses the same steps:

```cpp
auto valid = Vdevice::Validate_Device(config);  // offline descriptor validation
auto device = Vdevice::Create_Device(config);  // keep this move-only object alive
auto output = Vdevice::Get_Output(50);         // host OUT/control/state events
// Service delegated controls and wait for USB configuration, then:
auto sent = Vdevice::Send_Input(device, 0x81, bytes);
auto removed = Vdevice::Delete_Device(device);
```

Check each result's `error` and `message`. `Respond_Output(output, bytes, stall)` completes a delegated control; `Get_Info()` reports driver/demo state and owned devices. Creation and sending are asynchronous with respect to Windows enumeration and application consumption. Object destruction or process termination disconnects owned devices.

The [API guide](docs/API.md) explains every descriptor, rule and field. Model-specific bytes and encoders live in the **examples**, not the library. The additional **Descriptor Workshop** imports editable JSON and exchanges arbitrary raw reports; generic gamepad and custom HID console samples are also included.

## Downloads and source

| Release file | Contents |
| --- | --- |
| `VdeviceSetup.exe`, `VdeviceSetupConsole.exe` | Standalone graphical/console installers containing the same signed driver |
| `Vdevice-Examples-1.0.0.zip` | Five console/UI pairs, raw HID samples and Descriptor Workshop |
| `Vdevice-SDK-1.0.0.zip` | Public sources and build files |
| `Vdevice-Driver-1.0.0.zip` | Unmodified Microsoft-signed SYS, INF and catalog |
| `SHA256SUMS.txt` | SHA-256 hashes of the downloads |

```text
Vdevice/            Vdevice.h + Vdevice.cpp
ExamplesConsole/    Raw protocol definitions and console programs
ExamplesUI/         Control panels and Descriptor Workshop
InstallerConsole/   Console installer and native setup backend
InstallerUI/        UI installer and signed driver package
docs/ Tests/        API, installation and verification
```

Build with **Visual Studio 2022 Desktop C++**, a Windows SDK, CMake and .NET Framework reference assemblies:

```powershell
.\Build.ps1
```

This builds user-mode components and runs offline tests. It does not install a driver, create a device or rebuild the signed kernel. **Kernel source is not included.** User-mode source is MIT-licensed, with [third-party notices](licenses/NOTICE); prebuilt driver binaries have [demo distribution terms](licenses/BINARY-LICENSE.txt).

## Scope and related projects

Vdevice uses **KMDF / USB Device Emulation (UdeCx)**. Supported USB descriptions include low/full/high speed, one configuration, multiple interfaces at alternate setting zero, and interrupt/bulk endpoints. SuperSpeed, isochronous audio, external hubs and additional alternate settings/configurations are not implemented. Application code implements each device's actual protocol; changing a VID/PID alone does not recreate its firmware or authentication.

The Xbox 360 and DS4 examples use descriptor/protocol material from **[nefarius/ViGEmBus](https://github.com/nefarius/ViGEmBus)** under BSD-3-Clause. They use Vdevice's transport and do not require ViGEmBus to be installed. GIP follows Microsoft's **[MS-GIPUSB specification](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-gipusb/e7c90904-5e21-426e-b9ad-d82adeee0dbc)**.

For capturing and modifying **physical** keyboard, mouse and gamepad input, see **[Interceptor — Windows Kernel Filter Driver](https://github.com/TitanCSA/Interceptor)**. Vdevice creates virtual devices and is a separate driver.

[Installation, diagnostics and removal](docs/INSTALLATION.md) · [API reference](docs/API.md) · [Validation and limits](docs/VALIDATION.md)
