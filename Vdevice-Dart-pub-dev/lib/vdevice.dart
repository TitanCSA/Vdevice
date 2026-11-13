import 'dart:convert';
import 'dart:ffi';
import 'dart:io';
import 'dart:isolate';
typedef _OpenNative=Uint32 Function(Pointer<Uint64>);
typedef _CloseNative=Uint32 Function(Uint64);
typedef _CallNative=Pointer<Uint8> Function(Uint64,Pointer<Uint8>,Uint32);
typedef _FreeNative=Void Function(Pointer<Uint8>);
class NativeException implements Exception{final int code;final String message;NativeException(this.code,this.message);@override String toString()=>'$message (Windows $code)';}
class Client {
 final DynamicLibrary _library;
 late final int Function(Pointer<Uint64>) _open=_library.lookupFunction<_OpenNative,int Function(Pointer<Uint64>)>('vdevice_json_open');
 late final int Function(int) _close=_library.lookupFunction<_CloseNative,int Function(int)>('vdevice_json_close');
 late final Pointer<Uint8> Function(int,Pointer<Uint8>,int) _call=_library.lookupFunction<_CallNative,Pointer<Uint8> Function(int,Pointer<Uint8>,int)>('vdevice_json_call');
 late final void Function(Pointer<Uint8>) _free=_library.lookupFunction<_FreeNative,void Function(Pointer<Uint8>)>('vdevice_json_free');
 static final _kernel=DynamicLibrary.open('kernel32.dll');
 static final _alloc=_kernel.lookupFunction<Pointer<Void> Function(Uint32,UintPtr),Pointer<Void> Function(int,int)>('LocalAlloc');
 static final _release=_kernel.lookupFunction<Pointer<Void> Function(Pointer<Void>),Pointer<Void> Function(Pointer<Void>)>('LocalFree');
 int _session=0;
 Client._(this._library){
  final pointer=_alloc(0,8).cast<Uint64>();if(pointer==nullptr)throw StateError('Allocation failed');
  try{final code=_open(pointer);if(code!=0)throw NativeException(code,'Cannot open SDK');_session=pointer.value;}finally{_release(pointer.cast());}
 }
 static Future<Client> open({String? libraryPath})async{
  if(!Platform.isWindows || sizeOf<IntPtr>()!=8)throw UnsupportedError('Windows x64 required');
  var path=libraryPath??Platform.environment['VDEVICE_SDK_NATIVE_PATH'];
  if(path==null){final beside=File.fromUri(File(Platform.resolvedExecutable).uri.resolve('VdeviceJsonSDK.dll'));if(beside.existsSync()){path=beside.path;}else{final uri=await Isolate.resolvePackageUri(Uri.parse('package:vdevice/native/VdeviceJsonSDK.dll'));if(uri==null)throw StateError('Pass libraryPath or deploy DLL beside executable');path=uri.toFilePath();}}
  return Client._(DynamicLibrary.open(path));
 }
 dynamic call(String operation,[Map<String,dynamic> arguments=const{}]){
  if(_session==0)throw StateError('SDK is closed');final payload=utf8.encode(jsonEncode({'operation':operation,'arguments':arguments}));if(payload.length>1024*1024)throw ArgumentError('Request exceeds 1 MiB');
  final input=_alloc(0,payload.length).cast<Uint8>();if(input==nullptr)throw StateError('Allocation failed');
  try{input.asTypedList(payload.length).setAll(0,payload);final pointer=_call(_session,input,payload.length);if(pointer==nullptr)throw StateError('Native response allocation failed');
   try{var length=0;while(length<=16*1024*1024 && pointer[length]!=0){length++;}if(length>16*1024*1024)throw StateError('Invalid native response');final result=jsonDecode(utf8.decode(pointer.asTypedList(length))) as Map<String,dynamic>;if(result['ok']!=true)throw NativeException(result['code'] as int,result['message'] as String);return result['result'];}finally{_free(pointer);}
  }finally{_release(input.cast());}
 }
 void close(){if(_session!=0){final code=_close(_session);if(code!=0)throw NativeException(code,'Native close failed');_session=0;}}
}
