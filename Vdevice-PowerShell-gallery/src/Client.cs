using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
namespace TitanCSA.Products.Vdevice.PowerShell;
public sealed class NativeException(uint code,string message):Exception(message){public uint Code{get;}=code;}
public sealed class Client:IDisposable {
 private readonly object gate=new();private ulong session;
 public Client(){if(!OperatingSystem.IsWindows() || IntPtr.Size!=8)throw new PlatformNotSupportedException("Windows x64 required");var code=Native.Open(out session);if(code!=0)throw new NativeException(code,"Cannot open SDK");}
 public string Call(string operation,string argumentsJson="{}"){
  using var args=JsonDocument.Parse(argumentsJson);if(args.RootElement.ValueKind!=JsonValueKind.Object)throw new ArgumentException("Arguments must be a JSON object");
  var payload=JsonSerializer.SerializeToUtf8Bytes(new{operation,arguments=args.RootElement});if(payload.Length>1024*1024)throw new ArgumentException("Request exceeds 1 MiB");
  lock(gate){if(session==0)throw new ObjectDisposedException(nameof(Client));var pointer=Native.Call(session,payload,(uint)payload.Length);if(pointer==0)throw new OutOfMemoryException("Native response allocation failed");
   string text;try{text=Marshal.PtrToStringUTF8(pointer)??throw new InvalidDataException("Invalid native response");}finally{Native.Free(pointer);}
   using var result=JsonDocument.Parse(text);var root=result.RootElement;if(!root.GetProperty("ok").GetBoolean())throw new NativeException(root.GetProperty("code").GetUInt32(),root.GetProperty("message").GetString()??"Native error");return root.GetProperty("result").GetRawText();
  }
 }
 public void Dispose(){lock(gate){if(session!=0){var code=Native.Close(session);if(code!=0)throw new NativeException(code,"Native close failed");session=0;}}GC.SuppressFinalize(this);}
 ~Client(){try{Dispose();}catch{}}
}
internal static class Native {
 const string Library="VdeviceJsonSDK";
 static Native(){NativeLibrary.SetDllImportResolver(typeof(Native).Assembly,Resolve);}
 static nint Resolve(string name,Assembly assembly,DllImportSearchPath? search){if(name!=Library)return 0;var path=Environment.GetEnvironmentVariable("VDEVICE_SDK_NATIVE_PATH");if(path!=null)return NativeLibrary.Load(Path.GetFullPath(path));var dll="VdeviceJsonSDK.dll";foreach(var file in new[]{Path.Combine(AppContext.BaseDirectory,dll),Path.Combine(AppContext.BaseDirectory,"runtimes","win-x64","native",dll),Path.Combine(Path.GetDirectoryName(assembly.Location)!,dll)})if(File.Exists(file))return NativeLibrary.Load(file);throw new DllNotFoundException(dll);}
 [DllImport(Library,EntryPoint="vdevice_json_open",CallingConvention=CallingConvention.Cdecl)]internal static extern uint Open(out ulong id);
 [DllImport(Library,EntryPoint="vdevice_json_close",CallingConvention=CallingConvention.Cdecl)]internal static extern uint Close(ulong id);
 [DllImport(Library,EntryPoint="vdevice_json_call",CallingConvention=CallingConvention.Cdecl)]internal static extern nint Call(ulong id,byte[] request,uint length);
 [DllImport(Library,EntryPoint="vdevice_json_free",CallingConvention=CallingConvention.Cdecl)]internal static extern void Free(nint response);
}
