# Raw C++ API and USB mounting guide

Use `Vdevice.h` and `Vdevice.cpp` together in a Windows C++17 application. The SDK transports exact descriptors and raw reports; it contains no device presets, typed states or encoders. Keyboard, mouse, gamepad, composite and custom protocols follow the same flow:

1. Assemble `Device_Config` from descriptor bytes and optional control rules.
2. `Validate_Device(config)` offline and handle its diagnostic.
3. `Create_Device(config)` and keep the returned object alive.
4. Service `Get_Output()` during enumeration. Observe configuration in `Get_Info().devices`.
5. `Send_Input(device, endpoint, bytes)` and keep consuming host outputs/controls.
6. Send the protocol's release/neutral report if needed, then `Delete_Device(device)`.

The [graphical examples](../ExamplesUI) contain complete independent descriptor construction and this lifecycle. Their local construction functions are example code, not SDK operations. The [raw JSON sample](../ExamplesUI/CustomDevice.json) is another editable description; see its [workshop instructions](../ExamplesUI/README.md).

## Seven operations

All functions belong to namespace `Vdevice`. `Result.error == 0` means success; `message` provides a failure diagnostic. Error numbers are Windows error codes. `Result`, `Info` and `Output` have explicit Boolean conversions. `Device` has its own error/message and Boolean conversion.

```cpp
Result Validate_Device(const Device_Config& config);
Device Create_Device(const Device_Config& config);
Result Delete_Device(Device& device);
Result Send_Input(const Device& device, std::uint8_t endpoint,
                  const std::vector<std::uint8_t>& bytes);
Output Get_Output(std::uint32_t timeout = Infinite);
Result Respond_Output(const Output& output,
                      const std::vector<std::uint8_t>& bytes = {},
                      bool stall = false);
Info Get_Info();
```

### Validate_Device

Checks descriptor syntax, topology, report sizes, endpoint limits, rules and the serialized kernel request. It performs no driver open/installation and requires no administrator rights. The kernel validates creation again. A valid description does not establish compatibility with a specific child driver or application.

```cpp
auto check = Vdevice::Validate_Device(config);
if (!check) std::cerr << check.error << ": " << check.message;
```

### Create_Device

Validates before opening the driver, then plugs in the described device. Live access requires elevation. Failure returns a false `Device` with `error` and `message`. Success starts the shared trial if necessary; Windows enumeration continues asynchronously.

`Device` is move-only. Its string `token` identifies the device in this SDK session; do not parse it or persist it as a hardware serial number. `device.Id()` exposes the kernel identifier for correlation. Pass the object itself to send/delete. Destruction disconnects its device; process termination triggers kernel owner cleanup. Changing `token` does not change USB identity.

Configuration bytes are copied on creation. Editing `config` afterward does not change the live device. Delete/recreate it so Windows enumerates the updated description.

### Delete_Device

Disconnects the device and releases its kernel resources. Outstanding transfers complete with removal/cancellation. After successful removal, the object becomes false. Repeating a successful deletion succeeds; a default/moved-from object has no valid handle. Expiry or OS removal may return disconnection/not-found even though the object is then marked deleted. Destruction remains a fallback on early exit.

### Send_Input

Sends device-to-host data through a declared USB **IN** endpoint, for example `0x81`. Vector length is automatic. HID length and Report ID must match the descriptor. Other payloads follow the application's protocol. Endpoint zero is reserved for controls handled by `Respond_Output`.

```cpp
auto sent = Vdevice::Send_Input(device, 0x81, {42, 0, 0, 0});
if (!sent) std::cerr << sent.message;
```

Success means accepted/queued, not consumed by a game/application. Queuing can precede a host read. A full bounded queue returns an error; pace production according to host consumption and inspect counters. The SDK inserts no input polling sleep and does not invent a neutral report.

### Get_Output

Receives one event for the session, across all its devices. Timeout is milliseconds: `0` polls, a finite value waits, `Infinite` (default `0xffffffff`) waits until an event/disconnection. Correlate `output.device_token` with `device.token` or `Get_Info().devices`.

Timeout returns `timed_out=true`, `error=ERROR_TIMEOUT` (`1460`) and a false Boolean result. Check it before reporting other errors:

```cpp
auto output = Vdevice::Get_Output(100);
if (output.timed_out) {
    // No event within 100 ms.
} else if (!output) {
    std::cerr << output.message;
} else if (output.kind == Vdevice::Output_Kind::Data) {
    // Consume output.data according to this device's protocol.
} else if (output.kind == Vdevice::Output_Kind::Control) {
    // Unknown request: explicitly stall it.
    auto result = Vdevice::Respond_Output(output, {}, true);
    if (!result && result.error != 1168) std::cerr << result.message;
}
```

| Output field | Meaning |
| --- | --- |
| `error`, `message`, `timed_out` | Operation result and timeout indicator. |
| `device_token`, `device_id` | Source device in this session. |
| `kind` | `Data`, `Control` or `DeviceState`. |
| `endpoint` | Host OUT endpoint; zero for controls/HID SET_REPORT. |
| `setup` | Eight raw USB SETUP bytes when relevant. |
| `data` | OUT payload, control OUT data or device-state bytes. |
| `request_id` | Nonzero for a delegated control requiring a response. |

`Data` may include already-acknowledged HID SET_REPORT at endpoint zero. Its `request_id=0`: do not respond again. `DeviceState.data` holds four little-endian flag bytes: bit 0 plugged, bit 1 configured, bit 2 powered. `Get_Info()` provides named snapshot fields.

### Respond_Output

Completes one pending delegated `Control`. For IN, return protocol bytes. For a handled OUT, reply empty. For unsupported requests, `Respond_Output(output, {}, true)` stalls. Stall data must be empty. Replies are bounded by host length/capacity and 4096 bytes.

The driver bounds delegated controls to approximately five seconds. Respond during enumeration as well as ordinary I/O. Cancellation, timeout, removal or a duplicate response can produce `ERROR_NOT_FOUND` (`1168`); handle this expected race. Never respond to arbitrary data/state events or reuse request IDs.

### Get_Info

Reads capabilities/trial state and this session's active devices. It opens the live API if needed, requires the same rights, and does not start the trial. Query/list results are observational snapshots; Windows may continue enumeration/removal afterward.

| Info field | Meaning |
| --- | --- |
| `error`, `message` | Query/list result; check first. |
| `version` | Wire protocol version, currently 1. |
| `max_devices`, `max_transfer` | Currently 16 and 4096. |
| `low_speed`, `full_speed`, `high_speed` | Available emulated speeds. |
| `trial_started`, `trial_expired` | Shared state for the current kernel boot. |
| `trial_remaining_100ns` | Remaining budget; divide by 10,000,000 for seconds. |
| `quiescing`, `removing` | Controller stopping for uninstall/removal; release work/handles. |
| `devices` | Vector of session-owned `Device_Details`, not a physical-device inventory. |

| Device_Details field | Meaning/example |
| --- | --- |
| `token`, `id` | Correlate with `device.token`, `device.Id()` or output. |
| `plugged`, `configured`, `powered` | USB/PnP state; configuration can finish after Create_Device. |
| `endpoint_count` | Non-control endpoints; excludes EP0. |
| `queued_inputs`, `queued_outputs` | Outstanding input packets and owner events for this device. |
| `inputs_accepted`, `inputs_completed` | Cumulative accepted sends and completed transfers toward host. |
| `outputs_received`, `outputs_dropped` | Cumulative accepted events and events dropped without storage. |
| `error` | Per-entry error; currently zero for successfully listed entries. |

```cpp
const auto info = Vdevice::Get_Info();
if (info) {
    for (const auto& details : info.devices)
        if (details.token == device.token)
            std::cout << "Configured=" << details.configured
                      << ", queued=" << details.queued_inputs;
}
```

## Configuration objects

`Device_Config` contains only five fields. Identity, names, report layout, packet sizes, intervals and power declarations come from descriptor bytes.

| Field | Default and role |
| --- | --- |
| `speed` | `Speed::Full`; choose Low/Full/High with matching valid descriptors. |
| `input_depth` | 32; preallocated packets per device across IN endpoints, range 1–256. |
| `delegate_controls` | false; deliver unmatched class/vendor controls when true. |
| `descriptors` | Vector of exact Descriptor entries. |
| `rules` | Vector of ordered static Control_Rule entries. |

A `Descriptor` has `kind`, `index`, `language`, `interface_number` and `data`. Defaults: `Standard`, selectors zero, empty bytes. Standard type comes from `data[1]`.

| Entry | Selectors and bytes |
| --- | --- |
| Device | Standard, selectors zero; complete 18-byte type 01 descriptor. |
| Configuration | Standard, selectors zero; complete type 02 tree including interfaces/class/endpoints. |
| String | Standard; index=string ID, language=LANGID, interface_number=0; complete type 03. |
| BOS | Standard, selectors zero; complete type 0F tree. |
| HID report | Report, index=0, language=0, interface_number=HID interface; raw items. |

Concatenate interface/endpoint/HID class descriptors inside the configuration, not as independent entries. Duplicate selectors are rejected. Exactly one device and one configuration are required, with one report entry per HID interface and none for non-HID interfaces.

## Mounting stage 1: USB device identity

Multibyte integers below are little-endian. Offsets are zero-based. Set identity in the descriptor; Device_Config has no duplicate identity fields.

| Offset | Field | Value/meaning |
| --- | --- | --- |
| 0, 1 | bLength, bDescriptorType | 18, 1. |
| 2–3 | bcdUSB | BCD revision `0x0110`–`0x0210`; bytes `00 02` mean USB 2.00. |
| 4–6 | Class/subclass/protocol | Device identity; class zero commonly selects classes per interface. |
| 7 | bMaxPacketSize0 | Low: 8; full: 8/16/32/64; high: 64. |
| 8–9, 10–11 | idVendor, idProduct | Assigned identity; examples use development placeholder FFFF. |
| 12–13 | bcdDevice | Revision; 00 01 means 1.00. |
| 14–16 | String indices | Manufacturer, product, serial; zero omits each. |
| 17 | bNumConfigurations | Exactly 1. |

See Microsoft's [USB device descriptor definition](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/usbspec/ns-usbspec-_usb_device_descriptor). Ranges above are Vdevice's validator limits; high speed requires bcdUSB at least `0x0200`. Copying a product's VID/PID or strings does not implement its protocol or make an incompatible driver bind.

## Mounting stage 2: configuration and interfaces

Start with a nine-byte configuration header, then append each interface and its descriptors. Total length equals the whole vector, including every child descriptor.

| Offset | Field | Value/meaning |
| --- | --- | --- |
| 0–1 | Length/type | 9, 2. |
| 2–3 | wTotalLength | Entire tree size, little-endian. |
| 4 | bNumInterfaces | Number of interface descriptors. |
| 5 | bConfigurationValue | Nonzero ID, commonly 1. |
| 6 | iConfiguration | String index or zero. |
| 7 | bmAttributes | Bit 7 required; bit 6 self-powered, bit 5 remote wake; bits 0–4 zero. |
| 8 | bMaxPower | 2 mA units, at most 250; 50 declares 100 mA. |

Power/wake bits describe capabilities; setting them does not implement wake behavior. See the [configuration descriptor definition](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/usbspec/ns-usbspec-_usb_configuration_descriptor).

Each interface descriptor is nine bytes:

```text
09 04  II 00  NN CC SS PP XX
       |  |   |  |  |  |  +-- iInterface string index (or 0)
       |  |   |  +--+--+----- class, subclass, protocol
       |  |   +-------------- following non-control endpoint count
       |  +------------------ alternate setting: 0 only
       +--------------------- interface number
```

Interface numbers are unique and contiguous from zero. For multiple interfaces, append interface 0's class/endpoint descriptors, then interface 1 and its descriptors; update counts/lengths. Endpoint addresses remain unique across the device. HID class is `03`; subclass/protocol `01/01` declares boot keyboard, `01/02` boot mouse. Ordinary non-boot HID uses `00/00`. Other classes need the descriptors, requests and behavior expected by their Windows drivers; Vdevice does not automatically implement those protocols.

## Mounting stage 3: endpoints and direction

A non-control endpoint descriptor is exactly seven bytes:

```text
07 05  AA TT LL HH II
       |  |  +--+  +-- bInterval
       |  |  wMaxPacketSize (little-endian)
       |  +-------------- type: 02 bulk, 03 interrupt
       +----------------- address: bit 7 IN, bits 0–3 endpoint number
```

Direction is from the host's perspective: **IN** means virtual device → Windows (`Send_Input`); **OUT** means Windows → virtual device (`Get_Output`). `0x81` is IN endpoint 1; `0x01` is OUT endpoint 1. These may coexist. Numbers are 1–15. Endpoint zero is implicit, reserved for controls. Address bits 4–6 must be zero. Isochronous/high-bandwidth transactions are unsupported.

| Speed | Interrupt | Bulk |
| --- | --- | --- |
| Low | 1–8 byte packet; interval at least 10 ms. | Unsupported. |
| Full | 1–64 byte packet; nonzero interval in ms. | Packet size 8, 16, 32 or 64. |
| High | 1–1024 byte packet; interval 1–16. | Packet size 512. |

High-speed interrupt interval encodes `2^(bInterval-1)` microframes of 125 μs. Windows/emulation scheduling determines actual completions; this is not a latency measurement. Reports/transfers may span packets; each transfer is capped at 4096 bytes. See the [endpoint descriptor definition](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/usbspec/ns-usbspec-_usb_endpoint_descriptor).

Every HID interface has exactly one interrupt IN endpoint and may have one interrupt OUT endpoint. Non-HID interfaces can use the supported bulk/interrupt topology.

## Mounting stage 4: HID class and report descriptors

Inside the configuration, after its HID interface, append this nine-byte class descriptor before endpoints:

```text
09 21 11 01 00 01 22 LL HH
      +--+  |  |  | +--+-- report descriptor byte length
      HID 1.11 |  |  +------ descriptor type: Report (22)
              |  +--------- exactly one subordinate descriptor
              +------------ country code (0 if not localized)
```

Supply actual report bytes separately with `kind=Report` and matching `interface_number`. They have no bLength/type wrapper. The HID class descriptor's report length must exactly match that vector. Fill **both** length bytes when constructing it:

```cpp
configuration.data[25] = static_cast<std::uint8_t>(report.data.size());
configuration.data[26] = static_cast<std::uint8_t>(report.data.size() >> 8);
// Offsets 25/26 apply to a 9-byte config + 9-byte interface + 9-byte HID header.
```

HID items define usages, collections, logical ranges, widths, counts and Input/Output/Feature fields: they define the raw report format. Size 16 × Count 2 contributes four payload bytes. Repeated usages can belong to different collections: the gamepad example has two separate Physical collections for X/Y and preserves all six axes.

| Example bytes | Meaning |
| --- | --- |
| 05 01 | Usage Page: Generic Desktop. |
| 09 05 | Usage: Game Pad. |
| A1 01 / C0 | Begin Application collection / end collection. |
| 09 30, 09 31 | X/Y usages on Generic Desktop page. |
| 16 00 80 / 26 FF 7F | Signed logical minimum -32768 / maximum 32767. |
| 75 10, 95 02 | Report Size 16 bits, Count 2. |
| 81 02 / 81 06 | Input Data/Variable/Absolute / Relative. |
| 91 02, B1 02 | Output / Feature Data/Variable/Absolute. |
| 85 NN | Report ID NN, from 1–255. |

Fields pack in descriptor order. Declare Constant padding where needed; do not assume a C++ struct matches the layout. Logical values, button ordering, units and neutral values belong to the application protocol. See the [HID 1.11 specification](https://www.usb.org/sites/default/files/hid1_11.pdf) for the full item language.

**Report IDs:** without ID items, send only payload, without a leading zero. With IDs, byte 0 is the declared nonzero ID and counts in length. Windows HID clients may use a zero prefix for unnumbered reports at their API boundary; that does not add an ID byte to raw USB IN. Do not mix numbered/unnumbered layouts in one interface.

The driver checks HID IDs/exact lengths, caches valid reports and handles generic requests. It does not decode application values. Parser limits: 64 nested collections, 16 nested global Push items, 256 report layouts, 4096 bytes per report.

The gamepad example's 17-byte report:

| Offset | Payload |
| --- | --- |
| 0–3 | First X/Y pair, signed 16-bit little-endian. |
| 4–7 | Second X/Y pair, signed 16-bit little-endian. |
| 8–11 | Z/Rz, signed 16-bit little-endian. |
| 12–15 | 32 button bits; button 1 is bit 0. |
| 16 | Hat 1 low nibble, hat 2 high nibble: 0–7 direction, 8 released/null. |

Its neutral report ends in `88`, all preceding bytes zero. There is no universal reset/neutral SDK operation.

## Mounting stage 5: strings and languages

Set string indices in device/configuration/interface bytes, then supply matching standard string entries. A string is bLength, type `03`, and UTF-16LE code units **without a terminator**. Length is even, from 2–254 bytes. Index 0 is a language list rather than text:

```cpp
Vdevice::Descriptor languages;
languages.data = {4, 3, 0x09, 0x04}; // LANGID 0409, English (US).
Vdevice::Descriptor product;
product.index = 2;
product.language = 0x0409;
product.data = {8, 3, 'P', 0, 'a', 0, 'd', 0}; // "Pad"
config.descriptors.push_back(languages);
config.descriptors.push_back(product);
// Set device descriptor byte 15 (iProduct) to 2.
```

Use `language=0` for string 0 and a nonzero LANGID for ordinary strings. Translations use the same index with different language selectors, listed in string 0. The special Microsoft OS string index 0xEE can use language 0; declaring it requires the corresponding OS protocol, which is not synthesized. See Microsoft's [USB string descriptor documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/usb-string-descriptors).

## Mounting stage 6: BOS and additional descriptors

An optional BOS entry uses Standard, selectors zero and type `0F`. Its header is `05 0F LL HH NN`: LL/HH is the complete tree length and NN the capability count. Append that many capabilities, each with bLength at least 3, type `10`, its capability type and payload. Vdevice validates framing/count and forwards supplied bytes.

Advertise only capabilities your protocol supports. A capability does not enable SuperSpeed, unsupported link features or a proprietary protocol in the driver. Class descriptors can be embedded in the configuration with valid lengths. Supported standalone standard entries are device, configuration, string and BOS, plus separate HID reports. Other descriptor/control exchanges can use explicit static rules where supported.

## Mounting stage 7: host controls and OUT data

The eight USB SETUP bytes are:

| Offset | Field |
| --- | --- |
| 0 | bmRequestType: direction bit 7, type bits 5–6, recipient bits 0–4. |
| 1 | bRequest. |
| 2–3 | wValue, little-endian. |
| 4–5 | wIndex, little-endian; often the interface number. |
| 6–7 | wLength, little-endian. |

A `Control_Rule` has `setup[8]`, `mask[8]`, `action` and response `data`. Every byte must satisfy `(received & mask) == (setup & mask)`; zero mask ignores that byte. Rules run in vector order: first match wins, before generic HID handling.

| Action | Behavior |
| --- | --- |
| Rule_Action::Reply | Static IN response, clipped to host length/capacity. |
| Rule_Action::Accept | Acknowledge an implemented OUT without response data. |
| Rule_Action::Stall | Reject the request. |

Accept/Stall data must be empty. Reply data is limited to 4096 bytes. Example: a four-byte version on vendor IN request C0/5A, exact value/index zero, any host length:

```cpp
Vdevice::Control_Rule version;
version.setup = {0xC0, 0x5A, 0, 0, 0, 0, 0, 0};
version.mask  = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0};
version.data  = {0x56, 0x44, 1, 0};
config.rules.push_back(version);
```

For stateful behavior, set `delegate_controls=true`. Unmatched class/vendor requests arrive as Control events. Missing standard descriptors fail promptly unless explicitly supplied by a rule; delegation does not send every standard request to user mode. Inspect SETUP/OUT data, respond promptly and handle cancellation/the five-second deadline.

Generic HID handling supplies HID/report descriptors, cached GET_REPORT, valid SET_REPORT delivery, GET_IDLE, zero-duration SET_IDLE and GET/SET_PROTOCOL for declared boot interfaces. An uncached Feature GET_REPORT can be delegated for application data. Nonzero SET_IDLE requests periodic reports: only acknowledge it if your application implements that contract. The custom example stalls unknown requests rather than claiming unsupported behavior succeeded.

Interrupt/bulk OUT events and already-handled SET_REPORT data have `request_id=0`; consume them without responding again. Keep draining outputs even when mainly sending input: keyboards receive LED updates and other protocols depend on host commands.

## Bounds, lifetime and deployment

| Resource | Current bound |
| --- | --- |
| Active devices | 16 across the controller. |
| Non-control endpoints | 30 per device, unique addresses. |
| Descriptor entries / static rules | 64 / 32 per device. |
| Serialized create | 65,536 bytes including headers, tables, descriptors/rules. |
| One transfer/report or control reply | 4096 bytes. |
| Input storage | input_depth packets per device across IN endpoints, 1–256. |
| Owner output queue | 64 events across the session's devices. |
| Pending delegated controls | 32 per device, also constrained by owner output storage. |

Use a bounded output loop rather than repeatedly creating devices or busy polling. Blocking Get_Output can run on a worker while another thread sends; coordinate deletion/shutdown. Sharing a Device does not make concurrent C++ destruction or unsynchronized mutation safe. Treat disconnection, expiry and uninstall quiescence as reasons to stop and release objects.

The driver owns devices by their open session, not a copied token. Tokens correlate events, not access rights for another process. Live access is restricted to administrators/system; Validate_Device remains offline.

The trial lasts 3,600 seconds after first successful creation per kernel boot, shared across applications. Queries do not start it. Expiry disconnects devices, wakes requests and prevents creation/transfers. Reinstalling or restarting an application does not reset it; Windows Restart does. Sleep counts toward the budget.

Supported topology/validation do not guarantee every Windows class driver or proprietary product. Applications implement the protocols they advertise. See [installation](INSTALLATION.md) and [validation](VALIDATION.md).
