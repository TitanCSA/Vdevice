import ctypes
import json
import os
import threading
from pathlib import Path

class NativeError(RuntimeError):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code

class Client:
    """Direct Windows x64 SDK session. Use as a context manager or close explicitly."""
    def __init__(self, library_path=None):
        if os.name != 'nt' or ctypes.sizeof(ctypes.c_void_p) != 8:
            raise RuntimeError('Windows x64 required')
        path = library_path or os.environ.get('VDEVICE_SDK_NATIVE_PATH') or Path(__file__).with_name('VdeviceJsonSDK.dll')
        self._library = ctypes.CDLL(str(path))
        self._lock = threading.RLock()
        self._open = getattr(self._library, 'vdevice_json_open')
        self._open.argtypes = [ctypes.POINTER(ctypes.c_uint64)]
        self._open.restype = ctypes.c_uint32
        self._close = getattr(self._library, 'vdevice_json_close')
        self._close.argtypes = [ctypes.c_uint64]
        self._close.restype = ctypes.c_uint32
        self._call = getattr(self._library, 'vdevice_json_call')
        self._call.argtypes = [ctypes.c_uint64, ctypes.c_char_p, ctypes.c_uint32]
        self._call.restype = ctypes.c_void_p
        self._free = getattr(self._library, 'vdevice_json_free')
        self._free.argtypes = [ctypes.c_void_p]
        self._free.restype = None
        self._session = ctypes.c_uint64()
        code = self._open(ctypes.byref(self._session))
        if code:
            raise NativeError(code, 'Cannot open product SDK session')

    def call(self, operation, arguments=None):
        payload = json.dumps({'operation': operation, 'arguments': {} if arguments is None else arguments}, ensure_ascii=False, allow_nan=False).encode('utf-8')
        if len(payload) > 1024 * 1024:
            raise ValueError('Request exceeds 1 MiB')
        with self._lock:
            if not self._session.value:
                raise RuntimeError('SDK session is closed')
            pointer = self._call(self._session.value, payload, len(payload))
            if not pointer:
                raise MemoryError('Native response allocation failed')
            try:
                result = json.loads(ctypes.string_at(pointer).decode('utf-8'))
            finally:
                self._free(pointer)
            if not result['ok']:
                raise NativeError(result['code'], result['message'])
            return result['result']

    def close(self):
        with self._lock:
            if self._session.value:
                code = self._close(self._session.value)
                if code:
                    raise NativeError(code, 'Native close failed')
                self._session.value = 0

    def __enter__(self):
        return self

    def __exit__(self, *args):
        self.close()
