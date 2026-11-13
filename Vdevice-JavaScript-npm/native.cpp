#include <node_api.h>
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <stdexcept>
#include <memory>
extern "C" __declspec(dllimport) unsigned vdevice_json_open(std::uint64_t*);
extern "C" __declspec(dllimport) unsigned vdevice_json_close(std::uint64_t);
extern "C" __declspec(dllimport) char* vdevice_json_call(std::uint64_t,const char*,std::uint32_t);
extern "C" __declspec(dllimport) void vdevice_json_free(char*);
namespace {
struct Environment {std::uint64_t id=0;};
void Check(napi_status status){if(status!=napi_ok)throw std::runtime_error("Invalid argument or allocation failure");}
void Error(napi_env env,unsigned code){napi_value message,error,value;Check(napi_create_string_utf8(env,"Native session operation failed",NAPI_AUTO_LENGTH,&message));Check(napi_create_error(env,nullptr,message,&error));Check(napi_create_uint32(env,code,&value));Check(napi_set_named_property(env,error,"code",value));Check(napi_throw(env,error));}
template<class F>napi_value Guard(napi_env env,F fn){try{return fn();}catch(const std::exception& e){napi_throw_error(env,nullptr,e.what());return nullptr;}catch(...){napi_throw_error(env,nullptr,"Native addon failed");return nullptr;}}
Environment* Context(napi_env env){void* data=nullptr;Check(napi_get_instance_data(env,&data));return static_cast<Environment*>(data);}
std::uint64_t Session(napi_env env,napi_value value){std::uint64_t id;bool lossless;Check(napi_get_value_bigint_uint64(env,value,&id,&lossless));if(!lossless)throw std::runtime_error("Invalid session identifier");return id;}
napi_value Open(napi_env env,napi_callback_info){return Guard(env,[&]{auto context=Context(env);std::uint64_t id=0;auto code=vdevice_json_open(&id);if(code){Error(env,code);return static_cast<napi_value>(nullptr);}context->id=id;napi_value value;Check(napi_create_bigint_uint64(env,id,&value));return value;});}
napi_value Close(napi_env env,napi_callback_info info){return Guard(env,[&]{napi_value arg;std::size_t count=1;Check(napi_get_cb_info(env,info,&count,&arg,nullptr,nullptr));if(count!=1)throw std::runtime_error("Expected session");auto id=Session(env,arg);auto code=vdevice_json_close(id);if(code){Error(env,code);return static_cast<napi_value>(nullptr);}if(Context(env)->id==id)Context(env)->id=0;napi_value value;Check(napi_get_undefined(env,&value));return value;});}
napi_value Call(napi_env env,napi_callback_info info){return Guard(env,[&]{napi_value args[2];std::size_t count=2;Check(napi_get_cb_info(env,info,&count,args,nullptr,nullptr));if(count!=2)throw std::runtime_error("Expected session and UTF-8 request");auto id=Session(env,args[0]);std::size_t length=0;Check(napi_get_value_string_utf8(env,args[1],nullptr,0,&length));if(length>1024*1024)throw std::runtime_error("Request exceeds 1 MiB");std::vector<char> input(length+1);Check(napi_get_value_string_utf8(env,args[1],input.data(),input.size(),&length));std::unique_ptr<char,void(*)(char*)> output(vdevice_json_call(id,input.data(),static_cast<std::uint32_t>(length)),vdevice_json_free);if(!output)throw std::runtime_error("Native response allocation failed");napi_value value;Check(napi_create_string_utf8(env,output.get(),NAPI_AUTO_LENGTH,&value));return value;});}
void Cleanup(void* data){auto context=static_cast<Environment*>(data);if(context->id)vdevice_json_close(context->id);delete context;}
napi_value Init(napi_env env,napi_value exports){return Guard(env,[&]{napi_property_descriptor methods[]={{"open",nullptr,Open,nullptr,nullptr,nullptr,napi_default,nullptr},{"call",nullptr,Call,nullptr,nullptr,nullptr,napi_default,nullptr},{"close",nullptr,Close,nullptr,nullptr,nullptr,napi_default,nullptr}};Check(napi_define_properties(env,exports,3,methods));auto context=std::make_unique<Environment>();Check(napi_set_instance_data(env,context.get(),nullptr,nullptr));Check(napi_add_env_cleanup_hook(env,Cleanup,context.get()));context.release();return exports;});}
}
NAPI_MODULE(NODE_GYP_MODULE_NAME,Init)
