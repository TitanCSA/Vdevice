# Vdevice C / JSON ABI

Direct adapter to the unchanged product C++ SDK. `vdevice_json_open(uint64_t*)`, `call(uint64_t,const char*,uint32_t)`, `close(uint64_t)`, `free(char*)`. Call returns allocated UTF-8 JSON: `{ok:true,result:...}` or `{ok:false,code,message}`. Always free the returned pointer with this DLL. Request: `{operation,arguments}`. One active session per library. Opening is inert.

Operations: check, status, connect, disconnect, send, receive, reply. See [schema](docs/API.md). Descriptor bytes and original licensing checks remain authoritative. No driver installed by the SDK.
