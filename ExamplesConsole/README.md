# Console examples

Install Vdevice, open an administrator terminal, and launch the desired EXE.
The interactive examples check installation, create one device and service host
requests while accepting commands. Closing the program removes the device.

| Example | Useful commands after launch |
| --- | --- |
| Keyboard | `tap key1 4`, `set modifiers 2 key1 5`, `reset` |
| Mouse | `set x 100 y -40`, `set buttons 1`, `reset` |
| Xbox360 | `set buttons 0x1000 lx 16000 lt 128`, `reset` |
| DualShock4 | `set buttons 2 hat 3 lx 180 rt 200`, `set touch1 1 touch1_x 900 touch1_y 400`, `reset` |
| XboxOneGip | `set buttons 0x1000 ly 12000 lt 512`, `set guide 1`, `reset` |

`set` accepts multiple field/value pairs and sends one complete state. Integers
can be decimal or use a `0x` prefix. Invalid fields/ranges are rejected before
changing the state. Relative mouse movement resets after sending; held buttons
remain until changed or reset. Keyboard usages follow USB HID values, not text.

`fields` displays current values; `info` shows driver counters and the shared
demo allowance. `raw ENDPOINT HEX-BYTES` sends exact report bytes. `tap FIELD
VALUE [MS]` temporarily changes one field (50 ms by default). `quit` disconnects.

`--help` and `--validate` do not open the driver. Protocol encoders and descriptors
are in each model's `.cpp`; `ExampleDevice.h` / `ExampleRuntime.h` contain the
example-only field handling and lifecycle. `ExampleEntry.h` selects console or
UI startup. The raw SDK remains only `Vdevice.h` and `Vdevice.cpp`.

`Gamepad.cpp` and `Custom.cpp` are additional low-level raw HID demonstrations,
with their own `--help` / `--validate` modes. The five control panels compile the
same protocol definitions; see [the UI examples](../ExamplesUI/README.md).
