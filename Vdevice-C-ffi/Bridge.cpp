#include "Runtime.h"
#include "Vdevice/Vdevice.h"
#include <map>
namespace SDK=Vdevice;
template<class Result> void Check(const Result& result){if(result.error)throw SdkError(result.error,result.message);}
struct ProductSession {
 std::map<std::uint64_t,SDK::Device> devices;
 std::map<std::uint64_t,SDK::Output> outputs;
 std::uint64_t next=1;
 ~ProductSession(){for(auto& entry:devices)try{SDK::Delete_Device(entry.second);}catch(...){}}
 SDK::Device_Config Config(const Json& a){
  SDK::Device_Config config;config.speed=static_cast<SDK::Speed>(Number(a,"speed",2,3));Require(static_cast<unsigned>(config.speed)>0,"Speed must be 1,2,3");config.input_depth=Number(a,"input_depth",32,256);config.delegate_controls=a.value("delegate_controls",false);
  const auto descriptors=a.value("descriptors",Json::array());Require(descriptors.is_array() && descriptors.size()<=64,"At most 64 descriptors");
  for(const auto& value:descriptors){Require(value.is_object(),"Descriptor must be object");SDK::Descriptor d;d.kind=static_cast<SDK::Descriptor_Kind>(Number(value,"kind",1,2));Require(static_cast<unsigned>(d.kind)>0,"Descriptor kind must be 1 or 2");d.index=Number(value,"index");d.language=Number(value,"language");d.interface_number=Number(value,"interface_number");d.data=Bytes(value.at("data"));config.descriptors.push_back(std::move(d));}
  const auto rules=a.value("rules",Json::array());Require(rules.is_array() && rules.size()<=32,"At most 32 control rules");
  for(const auto& value:rules){Require(value.is_object(),"Rule must be object");SDK::Control_Rule r;auto setup=Bytes(value.at("setup"),8),mask=Bytes(value.at("mask"),8);Require(setup.size()==8 && mask.size()==8,"Setup and mask must be exactly 8 bytes");std::copy(setup.begin(),setup.end(),r.setup.begin());std::copy(mask.begin(),mask.end(),r.mask.begin());r.action=static_cast<SDK::Rule_Action>(Number(value,"action",1,3));Require(static_cast<unsigned>(r.action)>0,"Rule action must be 1..3");r.data=Bytes(value.value("data",Json::array()));config.rules.push_back(std::move(r));}
  return config;
 }
 Json Invoke(const std::string& op,const Json& a){
  if(op=="check"){auto config=Config(a);Check(SDK::Validate_Device(config));return {{"valid",true}};}
  if(op=="status"){
   auto info=SDK::Get_Info();Check(info);Json list=Json::array();for(const auto& d:info.devices)list.push_back({{"token",d.token},{"id",std::to_string(d.id)},{"plugged",d.plugged},{"configured",d.configured},{"powered",d.powered},{"endpoint_count",d.endpoint_count},{"queued_inputs",d.queued_inputs},{"queued_outputs",d.queued_outputs},{"inputs_accepted",std::to_string(d.inputs_accepted)},{"inputs_completed",std::to_string(d.inputs_completed)},{"outputs_received",std::to_string(d.outputs_received)},{"outputs_dropped",std::to_string(d.outputs_dropped)},{"error",d.error}});
   return {{"version",info.version},{"max_devices",info.max_devices},{"max_transfer",info.max_transfer},{"low_speed",info.low_speed},{"full_speed",info.full_speed},{"high_speed",info.high_speed},{"trial_started",info.trial_started},{"trial_expired",info.trial_expired},{"trial_remaining_100ns",std::to_string(info.trial_remaining_100ns)},{"quiescing",info.quiescing},{"removing",info.removing},{"devices",list}};
  }
  if(op=="connect"){
   Require(devices.size()<16,"At most 16 devices per SDK session");auto config=Config(a);Check(SDK::Validate_Device(config));auto device=SDK::Create_Device(config);Check(device);Require(static_cast<bool>(device),"SDK returned an invalid device");auto id=next++;Json result={{"handle",std::to_string(id)},{"device_id",std::to_string(device.Id())},{"token",device.token}};devices.emplace(id,std::move(device));return result;
  }
  if(op=="disconnect" || op=="send"){
   auto id=Identifier(a,"handle");auto it=devices.find(id);if(it==devices.end())throw SdkError(6,"Unknown or disconnected device handle");
   if(op=="disconnect"){Check(SDK::Delete_Device(it->second));devices.erase(it);return Json::object();}
   auto data=Bytes(a.at("data"));Check(SDK::Send_Input(it->second,static_cast<std::uint8_t>(Number(a,"endpoint",0,255)),data));return {{"bytes",data.size()}};
  }
  if(op=="receive"){
   Require(outputs.size()<4096,"Reply to pending output requests before receiving more");auto output=SDK::Get_Output(Number(a,"timeout",0,100));Check(output);
   Json result={{"timed_out",output.timed_out},{"device_id",std::to_string(output.device_id)},{"request_id",std::to_string(output.request_id)},{"device_token",output.device_token},{"kind",static_cast<unsigned>(output.kind)},{"endpoint",output.endpoint},{"setup",output.setup},{"data",output.data}};
   if(!output.timed_out && output.kind==SDK::Output_Kind::Control){auto id=next++;result["reply_handle"]=std::to_string(id);outputs.emplace(id,std::move(output));}return result;
  }
  if(op=="reply"){
   auto it=outputs.find(Identifier(a,"reply_handle"));if(it==outputs.end())throw SdkError(6,"Unknown or already replied request");auto data=Bytes(a.value("data",Json::array()));Check(SDK::Respond_Output(it->second,data,a.value("stall",false)));outputs.erase(it);return Json::object();
  }
  throw SdkError(50,"Unknown operation");
 }
};
static Runtime<ProductSession> runtime;
extern "C" __declspec(dllexport) unsigned vdevice_json_open(std::uint64_t* id) noexcept{return runtime.Open(id);}
extern "C" __declspec(dllexport) unsigned vdevice_json_close(std::uint64_t id) noexcept{return runtime.Close(id);}
extern "C" __declspec(dllexport) char* vdevice_json_call(std::uint64_t id,const char* request,std::uint32_t length) noexcept{return runtime.Call(id,request,length);}
extern "C" __declspec(dllexport) void vdevice_json_free(char* response) noexcept{delete[] response;}
