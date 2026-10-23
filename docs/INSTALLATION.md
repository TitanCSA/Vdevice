# Install, diagnose and remove Vdevice

Use Windows 10/11 x64. Native GIP requires Windows 10 21H2 or later. Installers
and device creation need administrator rights; offline descriptor checks do not.

1. Download `VdeviceSetup.exe` from [Releases](https://github.com/TitanCSA/Vdevice/releases/latest).
2. Accept UAC and choose **Install**, or **Reinstall / repair** if already installed.
   The installation, kernel driver and API cards update automatically.
3. Wait for **Installed and ready**. If Windows requests a restart, save your work
   and click **Restart Windows**, then confirm. Reopen the installer after restarting.
4. Open a control panel and choose **Create device**. Wait for **READY** before
   sending states.

The embedded driver is Microsoft-signed. No Test Mode, Secure Boot changes or
extra virtual-gamepad driver are required. The installer/example EXEs have their
own signature status; the driver's signature does not apply to those EXEs.

The installer distinguishes **Not installed**, **Package staged · not active**,
**Installed and ready**, **Installed · needs attention** and pending restart or
removal states. A service entry alone is not proof of a working installation.
Use **Refresh status** to reread Windows. Diagnostics and status checks do not
start the demo allowance or create a virtual device.

The restart button remains available while the current Windows session has a
pending Vdevice restart request, including after refreshing or reopening setup.
It never forces applications to close. If a removed package remains in use until
restart, reopen setup and use **Uninstall** again to finish package cleanup.

## Console setup

```powershell
.\VdeviceSetupConsole.exe /install
.\VdeviceSetupConsole.exe /status
.\VdeviceSetupConsole.exe /diagnose
.\VdeviceSetupConsole.exe /uninstall
```

Exit `0` means success; `3010` means Windows requires a restart; `1618` means
another Vdevice install/uninstall is running. Other values
indicate an error: retain the command's detailed output. Install/update checks
package integrity, catalog trust, PnP state, service state, the loaded driver
image and its API. It never automatically restarts Windows.

UI and console setup share the same operation lock and installation checks.
Install/uninstall retain the failed stage and Windows error in the detailed
report and reread Windows afterward. Removal verifies ownership before deleting
the Vdevice service; unrelated driver packages and services are preserved.

The graphical installer uses the same backend. Copy its report when requesting
help; remove personal paths or serials first. `--self-test` verifies the embedded
package without installation.

## Demo and cleanup

The one-hour allowance begins at the first successful virtual-device creation
in a Windows kernel boot and is shared by all programs. Expiry disconnects
virtual devices and refuses further creation/transfers. Closing a program or
reinstalling does not reset it. Use **Windows Restart** for a fresh allowance;
sleep counts toward the current allowance.

The returned device object owns its device. Deleting it, closing the application
or terminating its process disconnects it. Use **Release / neutral** to release
inputs while keeping the device connected, or **Disconnect** to remove it.
Physical input devices are not filtered by Vdevice.

## If a device does not work

- **Not installed:** run either installer. Examples do not silently install it.
- **Access denied:** accept the UI's elevation request or run the console as administrator.
- **Created, waiting:** Windows is still enumerating the USB device/protocol. Check the log and `/diagnose` output.
- **Transfer accepted:** bytes reached the driver queue; this alone is not proof that a game consumed them.
- **GIP security exchange requested:** confirm modern Windows and keep the documented USB opt-out GUID in the example metadata. Console authentication is not included.
- **Unknown raw controls:** the example stalls unsupported delegated requests rather than inventing successful replies.

The Descriptor Workshop requires its adjacent `VdeviceBridge.dll` and JSON
sample; the five dedicated native control panels are standalone EXEs. For GIP,
the example repeats the latest state briefly during startup and after changes
to cover Windows consumer initialization; it sends the first report immediately.

## Demo clock component

Setup includes the Microsoft-signed `TitanDemoClock.sys` companion. It keeps this
product's one-hour allowance in kernel memory until Windows Restart, independently
of unloading or reinstalling the main driver. It has no user-mode background process
and is not attached to the input stack. A product-owned driver service registers it;
removing another product does not remove this product's registration. Missing or
invalid clock files prevent protected operations; use setup to repair the package.
