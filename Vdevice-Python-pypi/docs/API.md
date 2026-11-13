# Device operations

All operations take an argument object; absent arguments mean `{}`.

- `check` and `connect`: `{speed:2,input_depth:32,delegate_controls:false,descriptors:[{kind:1,index:0,language:0,interface_number:0,data:[...]}],rules:[{setup:[8 bytes],mask:[8 bytes],action:1,data:[]}]}`. Exact descriptors define USB identity; check validates offline. Connect returns opaque decimal `handle`, actual decimal `device_id`, and token.
- `status`: returns capabilities, licensing and devices/counters. All uint64 values use decimal strings.
- `send`: `{handle,endpoint:129,data:[0,1,255]}` uses the explicit native endpoint overload.
- `disconnect`: `{handle}`. Close also disconnects owned devices.
- `receive`: `{timeout:0}`; bounded 0..100 ms per call so close cannot wait indefinitely. Poll from a worker if necessary. Returns kind, endpoint, setup, data, IDs and timed_out. Control outputs include opaque reply_handle.
- `reply`: `{reply_handle,data:[],stall:false}`. Failed replies may be retried; successful handles become stale. Maximum 4096 outstanding control requests.

JSON requests are bounded to 1 MiB, byte arrays to4096, descriptors64, rules32, devices16. Original SDK applies additional semantic validation. Handle values belong to the client session that returned them. Unknown or stale values are rejected within that session; do not transfer numeric handles between clients. Operations are serialized; concurrent operations return busy. Input capture/driver operations are performed only on explicit calls. Production tests only open/close and reject an invalid config offline. Synthetic tests cover the rest without real devices.

## Language client

The client calls the native SDK directly. Use the call method with an operation name and argument object. Receive is synchronous and bounded to100ms; use an application worker for responsive UI/event loops. Explicitly close the client after use. 64-bit identifiers and counters are decimal strings. Native errors expose their Windows code. Matching driver and licensing are separate requirements.
