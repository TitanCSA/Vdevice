# Install, diagnose and remove Vdevice

Use Windows 10/11 x64. Native GIP requires Windows 10 21H2 or later. Installers
and device creation need administrator rights; offline descriptor checks do not.

1. Download `VdeviceSetup.exe` from [Releases](https://github.com/TitanCSA/Vdevice/releases/latest).
2. Accept UAC and choose **Install / update**. Restart Windows if requested.
3. Open a control panel and choose **Create device**, or run a console sample
   from an administrator terminal. Wait for **READY** before sending states.

The embedded driver is Microsoft-signed. No Test Mode, Secure Boot changes or
extra virtual-gamepad driver are required. The installer/example EXEs have their
own signature status; the driver's signature does not apply to those EXEs.

## Console setup

```powershell
.\VdeviceSetupConsole.exe /install
.\VdeviceSetupConsole.exe /status
.\VdeviceSetupConsole.exe /diagnose
.\VdeviceSetupConsole.exe /uninstall
```

Exit `0` means success; `3010` means Windows requires a restart. Other values
indicate an error: retain the command's detailed output. Install/update checks
package integrity, catalog trust, PnP state, service state, the loaded driver
image and its API. It never automatically restarts Windows.

The graphical installer uses the same backend. Copy its report when requesting
help; remove personal paths or serials first. `--self-test` verifies the embedded
package without installation.

## Demo and cleanup

The two-hour allowance begins at the first successful virtual-device creation
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
