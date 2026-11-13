require 'json'
require 'fiddle'
require 'thread'
module VdeviceSDK
  class NativeError < StandardError
    attr_reader :code
    def initialize(code,message);@code=code;super(message);end
  end
  class Client
    def self.open(library_path: nil)
      client=new(library_path: library_path)
      return client unless block_given?
      begin;yield client;ensure;client.close;end
    end
    def initialize(library_path: nil)
      raise 'Windows x64 required' unless Fiddle::SIZEOF_VOIDP==8 && RUBY_PLATFORM.match?(/mingw|mswin/)
      @library=Fiddle::Handle.new(library_path || ENV['VDEVICE_SDK_NATIVE_PATH'] || File.expand_path('../native/VdeviceJsonSDK.dll',__dir__))
      @mutex=Mutex.new
      @open=Fiddle::Function.new(@library['vdevice_json_open'],[Fiddle::TYPE_VOIDP],Fiddle::TYPE_INT)
      @close=Fiddle::Function.new(@library['vdevice_json_close'],[Fiddle::TYPE_LONG_LONG],Fiddle::TYPE_INT)
      @call=Fiddle::Function.new(@library['vdevice_json_call'],[Fiddle::TYPE_LONG_LONG,Fiddle::TYPE_VOIDP,Fiddle::TYPE_INT],Fiddle::TYPE_VOIDP)
      @free=Fiddle::Function.new(@library['vdevice_json_free'],[Fiddle::TYPE_VOIDP],Fiddle::TYPE_VOID)
      out=Fiddle::Pointer.malloc(8,Fiddle::RUBY_FREE);out[0,8]=[0].pack('Q<');code=@open.call(out);raise NativeError.new(code,'Cannot open SDK') if code!=0
      @session=out[0,8].unpack1('Q<')
    end
    def call(operation,arguments={})
      payload=JSON.generate({operation:operation,arguments:arguments}).encode('UTF-8')
      raise ArgumentError,'Request exceeds 1 MiB' if payload.bytesize>1024*1024
      @mutex.synchronize do
        raise 'SDK is closed' if @session==0
        pointer=@call.call(@session,payload,payload.bytesize);raise NoMemoryError,'Native response allocation failed' if pointer.null?
        begin;response=JSON.parse(pointer.to_s);ensure;@free.call(pointer);end
        raise NativeError.new(response['code'],response['message']) unless response['ok']
        response['result']
      end
    end
    def close
      @mutex.synchronize do
        if @session!=0
          code=@close.call(@session);raise NativeError.new(code,'Native close failed') if code!=0
          @session=0
        end
      end
    end
  end
end
