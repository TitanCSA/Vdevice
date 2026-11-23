# Signed demo validation - 1.0.6.0

The primary Vdevice driver and required TitanDemoClock have valid Microsoft Windows Hardware Compatibility Publisher signatures. The two executable bodies and INF match the demo signing candidate, and kernel catalog membership was verified. Both installers embed the same verified package.

## WDF-001 controller teardown

The signed demo installed in an isolated Windows 11 build 26100 Hyper-V guest with Secure Boot enabled and test signing disabled. The running driver matched the submitted payload by SHA-256.

- 100 create/delete cycles and 100 early owner-close cycles passed.
- Ten full runs of the five virtual-device examples passed, including DualShock 4 and Xbox One/GIP. The Interceptor Gamepad & Controller service was running.
- The guest remained on the same boot with zero recorded bugchecks during the test. Both installers and the offline workshop checks also passed.

These results cover the stated guest configuration and the signed **demo**. The commercial Release is tracked separately. See [WDF-001](https://github.com/TitanCSA/Vdevice/issues/1) for the earlier incident and current delivery status.

## Demo allowance

The driver allows 3,600 seconds per Windows kernel boot, starting at the first successful virtual-device creation. Status queries leave the timer idle. Closing an app, unloading the primary driver, or reinstalling leaves the per-boot clock in place. Use Windows Restart for a new allowance. This release does not change the demo policy.
