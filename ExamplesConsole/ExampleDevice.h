#pragma once
#include "Vdevice.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Protocol definitions belong to the applications, never to the raw SDK.
namespace Examples {
using Bytes=std::vector<std::uint8_t>;
struct Field {const char* name;int minimum,maximum,initial,value;};
struct Button {const char* name;std::uint32_t mask;};
struct Packet {std::uint8_t endpoint=0x81;Bytes bytes;};
struct Reply {
    std::vector<Packet> packets;
    bool handled=false,stall=true;
    Bytes bytes;
    std::string text;
};
inline Vdevice::Descriptor Descriptor(Bytes data,Vdevice::Descriptor_Kind kind=Vdevice::Descriptor_Kind::Standard) {
    Vdevice::Descriptor descriptor;descriptor.kind=kind;descriptor.data=std::move(data);return descriptor;
}
inline std::array<std::uint8_t,6> Identity(const std::string& serial) {
    std::uint64_t hash=14695981039346656037ULL;
    for(unsigned char ch:serial){hash^=ch;hash*=1099511628211ULL;}
    std::array<std::uint8_t,6> result{};
    for(auto& ch:result){ch=static_cast<std::uint8_t>(hash);hash>>=8;}
    result[0]=static_cast<std::uint8_t>((result[0]&0xFC)|2);return result;
}
inline void Put16(Bytes& data,std::size_t offset,int value) {
    const auto word=static_cast<std::uint16_t>(value);
    data.at(offset)=static_cast<std::uint8_t>(word);
    data.at(offset+1)=static_cast<std::uint8_t>(word>>8);
}
inline std::string Hex(const Bytes& data) {
    std::ostringstream text;text<<std::hex<<std::uppercase<<std::setfill('0');
    for(std::size_t i=0;i<data.size();++i){if(i)text<<' ';text<<std::setw(2)<<unsigned(data[i]);}
    return text.str();
}
inline int Number(const std::string& text) {
    std::size_t used=0;const auto value=std::stoll(text,&used,0);
    if(used!=text.size()||value<(-2147483647LL-1)||value>2147483647LL)throw std::invalid_argument("Invalid integer: "+text);
    return static_cast<int>(value);
}
inline Vdevice::Descriptor String(std::uint32_t index,const std::string& text) {
    if(text.size()>126)throw std::invalid_argument("USB string is too long.");
    Vdevice::Descriptor descriptor;descriptor.index=index;descriptor.language=0x409;
    descriptor.data={static_cast<std::uint8_t>(2+2*text.size()),3};
    for(unsigned char ch:text){descriptor.data.push_back(ch);descriptor.data.push_back(0);}
    return descriptor;
}
inline void Strings(Vdevice::Device_Config& config,const std::string& manufacturer,const std::string& product,const std::string& serial) {
    Vdevice::Descriptor languages;languages.data={4,3,9,4};
    config.descriptors.push_back(std::move(languages));
    config.descriptors.push_back(String(1,manufacturer));config.descriptors.push_back(String(2,product));
    if(!serial.empty())config.descriptors.push_back(String(3,serial));
}
inline Vdevice::Control_Rule Rule(std::uint8_t type,std::uint8_t request,std::uint16_t value,std::uint16_t index,Bytes data) {
    Vdevice::Control_Rule rule;
    rule.setup={type,request,static_cast<std::uint8_t>(value),static_cast<std::uint8_t>(value>>8),static_cast<std::uint8_t>(index),static_cast<std::uint8_t>(index>>8),0,0};
    rule.mask={255,255,255,255,255,255,0,0};rule.data=std::move(data);return rule;
}
inline void CompatibleId(Vdevice::Device_Config& config,const char* id,std::uint8_t interfaces=1) {
    // Microsoft OS 1.0 string and extended compatible ID descriptor.
    Vdevice::Descriptor os;os.index=0xEE;os.data={18,3,'M',0,'S',0,'F',0,'T',0,'1',0,'0',0,'0',0,0x20,0};
    config.descriptors.push_back(std::move(os));
    Bytes data(40,0);data[0]=40;data[5]=1;data[6]=4;data[8]=1;data[17]=interfaces;
    for(std::size_t i=0;id[i]&&i<8;++i)data[18+i]=static_cast<std::uint8_t>(id[i]);
    config.rules.push_back(Rule(0xC0,0x20,0,4,std::move(data)));
}
class Protocol {
protected:
    void Add(const char* name,int low,int high,int initial=0){fields.push_back({name,low,high,initial,initial});}
public:
    std::vector<Field> fields;
    std::vector<Button> buttons;
    std::string title,description,legend;
    std::string button_field="buttons";
    bool relative=false;
    virtual ~Protocol()=default;
    int Value(const std::string& name) const {
        for(const auto& field:fields)if(name==field.name)return field.value;
        throw std::invalid_argument("Unknown field: "+name);
    }
    void Set(const std::string& name,int value) {
        for(auto& field:fields)if(name==field.name){
            if(value<field.minimum||value>field.maximum)throw std::out_of_range(name+" is outside "+std::to_string(field.minimum)+".."+std::to_string(field.maximum));
            field.value=value;return;
        }
        throw std::invalid_argument("Unknown field: "+name);
    }
    void Reset(){for(auto& field:fields)field.value=field.initial;}
    virtual Vdevice::Device_Config Config(const std::string& serial) const=0;
    virtual std::vector<Packet> Input()=0;
    virtual std::vector<Packet> Start(std::uint64_t){return Input();}
    virtual std::vector<Packet> Tick(std::uint64_t){return {};}
    virtual Reply Output(const Vdevice::Output& output,std::uint64_t) {
        Reply reply;
        if(output.request_id && output.setup[0]==0xA1 && output.setup[1]==1 && output.setup[3]==1 && output.setup[4]==0 && output.setup[5]==0) {
            auto input=Input();if(!input.empty()){reply.handled=true;reply.stall=false;reply.bytes=std::move(input.front().bytes);}
        }
        return reply;
    }
    virtual bool Ready() const{return true;}
};
}
