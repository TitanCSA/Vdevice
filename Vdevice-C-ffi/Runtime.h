#pragma once
#include <windows.h>
#include "third_party/json.hpp"
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
using Json = nlohmann::json;
struct SdkError : std::runtime_error {
    unsigned code;
    SdkError(unsigned c, const std::string& text):std::runtime_error(text),code(c){}
};
inline void Require(bool condition,const char* text){if(!condition)throw SdkError(87,text);}
inline unsigned Number(const Json& object,const char* key,unsigned fallback=0,unsigned maximum=0xffffffffu){
    if(!object.contains(key))return fallback;
    const auto& value=object.at(key);
    Require(value.is_number_integer() && (!value.is_number_integer() || value>=0),"Expected nonnegative integer");
    auto n=value.get<std::uint64_t>();Require(n<=maximum,"Integer out of range");return static_cast<unsigned>(n);
}
inline std::uint64_t Identifier(const Json& object,const char* key){
    const auto& text=object.at(key);Require(text.is_string(),"64-bit identifiers must be decimal strings");
    auto value=text.get<std::string>();Require(!value.empty() && value.size()<=20 && value.find_first_not_of("0123456789")==std::string::npos,"Invalid identifier");
    try{return std::stoull(value);}catch(...){throw SdkError(87,"Identifier out of range");}
}
inline std::vector<std::uint8_t> Bytes(const Json& value,std::size_t maximum=4096){
    Require(value.is_array() && value.size()<=maximum,"Invalid byte array size");std::vector<std::uint8_t> bytes;bytes.reserve(value.size());
    for(const auto& n:value){Require(n.is_number_integer() && n>=0 && n<=255,"Byte must be integer 0..255");bytes.push_back(n.get<std::uint8_t>());}return bytes;
}
inline std::wstring Wide(const std::string& text){
    Require(text.size()<1024*1024 && text.find('\0')==std::string::npos,"Invalid UTF-8 string size or embedded NUL");
    if(text.empty())return {};auto count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);Require(count>0,"Invalid UTF-8 string");std::wstring value(count,L'\0');MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),value.data(),count);return value;
}
inline std::string Utf8(const std::wstring& value){
    if(value.empty())return {};int count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);Require(count>0,"Invalid UTF-16 string");std::string text(count,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),text.data(),count,nullptr,nullptr);return text;
}
// Each product provides ProductSession::Invoke; no driver is opened by Open.
template<class ProductSession> class Runtime {
    std::mutex mutex;
    std::unique_ptr<ProductSession> state;
    std::uint64_t current=0,next=1;
    static char* Copy(const Json& response) noexcept {
        try{auto text=response.dump();if(text.size()>16*1024*1024)return nullptr;auto result=new char[text.size()+1];std::memcpy(result,text.c_str(),text.size()+1);return result;}catch(...){return nullptr;}
    }
public:
    unsigned Open(std::uint64_t* out) noexcept {
        if(!out)return ERROR_INVALID_PARAMETER;*out=0;
        try{std::lock_guard<std::mutex> lock(mutex);if(state)return ERROR_BUSY;if(!next)return ERROR_ARITHMETIC_OVERFLOW;state=std::make_unique<ProductSession>();current=next++;*out=current;return 0;}catch(...){return ERROR_NOT_ENOUGH_MEMORY;}
    }
    unsigned Close(std::uint64_t id) noexcept {
        try{std::lock_guard<std::mutex> lock(mutex);if(!state || id!=current)return ERROR_INVALID_HANDLE;state.reset();current=0;return 0;}catch(...){return ERROR_GEN_FAILURE;}
    }
    char* Call(std::uint64_t id,const char* request,std::uint32_t length) noexcept {
        try{
            std::unique_lock<std::mutex> lock(mutex,std::try_to_lock);if(!lock.owns_lock())throw SdkError(ERROR_BUSY,"Another operation is active");
            if(!state || id!=current)throw SdkError(ERROR_INVALID_HANDLE,"Closed or stale session");
            Require(request && length && length<=1024*1024,"Request must be 1 byte..1 MiB");auto value=Json::parse(request,request+length);Require(value.is_object(),"Request must be an object");
            auto op=value.at("operation").get<std::string>();Require(op.size()<=80,"Invalid operation");auto args=value.value("arguments",Json::object());Require(args.is_object(),"Arguments must be an object");
            return Copy({{"ok",true},{"result",state->Invoke(op,args)}});
        }catch(const SdkError& e){return Copy({{"ok",false},{"code",e.code},{"message",e.what()}});}
        catch(const Json::exception& e){return Copy({{"ok",false},{"code",87},{"message",e.what()}});}
        catch(const std::bad_alloc&){return Copy({{"ok",false},{"code",8},{"message","Out of memory"}});}
        catch(const std::exception& e){return Copy({{"ok",false},{"code",31},{"message",e.what()}});}
        catch(...){return Copy({{"ok",false},{"code",31},{"message","Native operation failed"}});}
    }
};
