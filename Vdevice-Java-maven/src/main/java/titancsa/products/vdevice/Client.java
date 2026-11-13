package titancsa.products.vdevice;
import java.lang.foreign.*;
import java.lang.invoke.MethodHandle;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.io.IOException;
import java.util.HexFormat;
import java.security.MessageDigest;

/** Direct UTF-8 JSON interface to the product's native SDK; close every session. */
public final class Client implements AutoCloseable {
 private static final String RESOURCE="/native/vdevice/win-x64/VdeviceJsonSDK.dll";
 private final Arena libraryArena=Arena.ofShared();
 private MethodHandle call,free,close;
 private long session;
 public Client(){this(System.getenv("VDEVICE_SDK_NATIVE_PATH"));}
 public Client(String path){
  if(!System.getProperty("os.name").startsWith("Windows") || ValueLayout.ADDRESS.byteSize()!=8)throw new IllegalStateException("Windows x64 required");
  try{
   if(path==null){byte[] bytes;try(var stream=Client.class.getResourceAsStream(RESOURCE)){if(stream==null)throw new IOException("Missing native resource");bytes=stream.readAllBytes();}
    var digest=HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(bytes));var directory=Path.of(System.getProperty("java.io.tmpdir"),"TitanCSA","vdevice",digest);Files.createDirectories(directory);var file=directory.resolve("VdeviceJsonSDK.dll");
    if(Files.exists(file)){if(!MessageDigest.isEqual(bytes,Files.readAllBytes(file)))throw new IOException("Native cache checksum mismatch");}else{var temporary=Files.createTempFile(directory,"native-",".tmp");try{Files.write(temporary,bytes);try{Files.move(temporary,file);}catch(FileAlreadyExistsException concurrent){if(!MessageDigest.isEqual(bytes,Files.readAllBytes(file)))throw new IOException("Native cache checksum mismatch");}}finally{Files.deleteIfExists(temporary);}}
    path=file.toString();
   }
   var symbols=SymbolLookup.libraryLookup(Path.of(path),libraryArena);var linker=Linker.nativeLinker();
   var open=linker.downcallHandle(symbols.find("vdevice_json_open").orElseThrow(),FunctionDescriptor.of(ValueLayout.JAVA_INT,ValueLayout.ADDRESS));
   close=linker.downcallHandle(symbols.find("vdevice_json_close").orElseThrow(),FunctionDescriptor.of(ValueLayout.JAVA_INT,ValueLayout.JAVA_LONG));
   call=linker.downcallHandle(symbols.find("vdevice_json_call").orElseThrow(),FunctionDescriptor.of(ValueLayout.ADDRESS,ValueLayout.JAVA_LONG,ValueLayout.ADDRESS,ValueLayout.JAVA_INT));
   free=linker.downcallHandle(symbols.find("vdevice_json_free").orElseThrow(),FunctionDescriptor.ofVoid(ValueLayout.ADDRESS));
   try(var arena=Arena.ofConfined()){var out=arena.allocate(ValueLayout.JAVA_LONG);int code=(int)open.invokeExact(out);if(code!=0)throw new NativeException(code,"Cannot open SDK");session=out.get(ValueLayout.JAVA_LONG,0);}
  }catch(Throwable error){libraryArena.close();throw failure(error);}
 }
 /** Returns the complete {ok,result} or {ok:false,code,message} JSON envelope. */
 public synchronized String call(String operation,String argumentsJson){
  if(session==0)throw new IllegalStateException("SDK is closed");
  if(!operation.matches("[A-Za-z_][A-Za-z0-9_]{0,79}"))throw new IllegalArgumentException("Invalid operation");
  var payload=("{\"operation\":\""+operation+"\",\"arguments\":"+argumentsJson+"}").getBytes(StandardCharsets.UTF_8);if(payload.length>1024*1024)throw new IllegalArgumentException("Request exceeds 1 MiB");
  try(var arena=Arena.ofConfined()){
   var input=arena.allocateFrom(ValueLayout.JAVA_BYTE,payload);var pointer=(MemorySegment)call.invokeExact(session,input,payload.length);if(pointer.address()==0)throw new OutOfMemoryError("Native response allocation failed");
   try{return pointer.reinterpret(16*1024*1024+1).getString(0);}finally{free.invokeExact(pointer);}
  }catch(Throwable error){throw failure(error);}
 }
 public String call(String operation){return call(operation,"{}");}
 @Override public synchronized void close(){if(session!=0){try{int code=(int)close.invokeExact(session);if(code!=0)throw new NativeException(code,"Native close failed");session=0;libraryArena.close();}catch(Throwable error){throw failure(error);}}}
 private static RuntimeException failure(Throwable error){return error instanceof RuntimeException r?r:new IllegalStateException("Native SDK call failed",error);}
 public static final class NativeException extends RuntimeException {public final int code;public NativeException(int code,String message){super(message);this.code=code;}}
}
