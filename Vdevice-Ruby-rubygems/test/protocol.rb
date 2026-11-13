require 'vdevice'
client=VdeviceSDK::Client.new(library_path:ARGV[0]);begin
 begin;client.call('check');raise 'Invalid config accepted';rescue VdeviceSDK::NativeError;end
 if ARGV[0];device=client.call('connect',{'descriptors'=>[{'data'=>[1,2,3]}]});raise 'uint64 lost' unless device['device_id']=='9223372036854775809';client.call('send',{'handle'=>device['handle'],'endpoint'=>129,'data'=>[0,1,255]});output=client.call('receive');client.call('reply',{'reply_handle'=>output['reply_handle'],'data'=>[0,1,255]});client.call('disconnect',{'handle'=>device['handle']});end
ensure;client.close;end
