#define VDEVICE_EXAMPLE_TEST
#include "../ExamplesUI/Keyboard.cpp"
#include "../ExamplesUI/Mouse.cpp"
#include "../ExamplesUI/Xbox360.cpp"
#include "../ExamplesUI/DualShock4.cpp"
#include "../ExamplesUI/XboxOneGip.cpp"
#include <iostream>
#include <random>

namespace {
std::uint64_t checks=0;
void Check(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
int Word(const Examples::Bytes& data,std::size_t offset){return int(data.at(offset))|(int(data.at(offset+1))<<8);}
void Fields(Examples::Protocol& model){
    Check(bool(Vdevice::Validate_Device(model.Config("offline-tests"))),"Descriptor configuration invalid");
    for(auto& field:model.fields){
        const auto name=std::string(field.name);const int low=field.minimum,high=field.maximum;
        model.Set(name,low);Check(model.Value(name)==low,"Minimum field value");model.Set(name,high);Check(model.Value(name)==high,"Maximum field value");
        bool invalid=false;try{model.Set(name,low-1);}catch(const std::out_of_range&){invalid=true;}Check(invalid&&model.Value(name)==high,"Lower range did not preserve state");
        invalid=false;try{model.Set(name,high+1);}catch(const std::out_of_range&){invalid=true;}Check(invalid&&model.Value(name)==high,"Upper range did not preserve state");
    }
    model.Reset();for(const auto& field:model.fields)Check(field.value==field.initial,"Neutral field value");
    bool invalid=false;try{model.Set("missing",1);}catch(const std::invalid_argument&){invalid=true;}Check(invalid,"Unknown field accepted");
}
Vdevice::Output Host(const Examples::Bytes& bytes){Vdevice::Output output;output.kind=Vdevice::Output_Kind::Data;output.data=bytes;return output;}
void Gip(){
    XboxOneGipExample::XboxOneGip model;Fields(model);
    auto hello=model.Start(1000);Check(hello.size()==1&&hello[0].bytes.size()==32&&hello[0].bytes[0]==2,"GIP Hello shape");
    Check(hello[0].bytes[16]==1&&hello[0].bytes[26]==1&&hello[0].bytes[28]==1&&hello[0].bytes[30]==1,"GIP firmware and protocol versions");
    Check(model.Tick(1499).empty()&&model.Tick(1500).size()==1,"GIP Hello interval");
    auto first=model.Output(Host({4,0x20,1,0}),1501);Check(first.packets.size()==1&&first.packets[0].bytes[1]==0xF0&&first.packets[0].bytes.size()==64,"GIP first metadata fragment");
    const auto sequence=first.packets[0].bytes[2];
    auto invalid=model.Output(Host({1,0x20,sequence,9,0,4,0x20,57,0,0,0,124,0}),1502);Check(invalid.packets.empty(),"GIP accepted wrong acknowledgement length");
    auto remaining=model.Output(Host({1,0x20,sequence,9,0,4,0x20,58,0,0,0,124,0}),1503);
    Check(remaining.packets.size()==3&&remaining.packets.back().bytes[1]==0xB0,"GIP remaining fragments");
    Examples::Bytes metadata(first.packets[0].bytes.begin()+6,first.packets[0].bytes.end());for(const auto& packet:remaining.packets)metadata.insert(metadata.end(),packet.bytes.begin()+6,packet.bytes.end());
    Check(metadata==Examples::Bytes(std::begin(ExampleDescriptors::GipMetadata),std::end(ExampleDescriptors::GipMetadata)),"GIP metadata reassembly");
    auto complete=model.Output(Host({1,0x20,sequence,9,0,4,0x20,198,0,0,0,0,0}),1504);
    Check(complete.packets.size()==1&&complete.packets[0].bytes==Examples::Bytes({4,0xA0,sequence,0,0xC6,1}),"GIP final handshake");
    auto active=model.Output(Host({5,0x20,3,1,0}),1505);Check(!model.Ready()&&active.packets.size()==2,"GIP START must wait for USB security status");
    model.Output(Host({6,0x20,4,2,1,0}),1506);Check(model.Ready(),"GIP USB security opt-out not accepted");
    model.Set("buttons",0xF3FF);model.Set("lt",1023);model.Set("rt",512);model.Set("lx",-32768);model.Set("ry",32767);model.Set("guide",1);
    auto input=model.Input();Check(input.size()==2&&input[0].bytes.size()==18&&Word(input[0].bytes,4)==0xFFFC,"GIP button mapping");
    Check(Word(input[0].bytes,6)==1023&&Word(input[0].bytes,8)==512&&Word(input[0].bytes,10)==32768&&Word(input[0].bytes,16)==32767,"GIP axis/trigger packing");
    Check(input[1].bytes[0]==7&&input[1].bytes[4]==1&&input[1].bytes[5]==0x5B,"GIP Guide down");
    Check(model.Input().size()==1,"GIP duplicate Guide event");model.Reset();Check(model.Input().back().bytes[4]==0,"GIP Guide release");
    model.Output(Host({5,0x20,4,1,7}),2000);Check(!model.Ready(),"GIP reset");
    model.Output(Host({4,0x20,5,0}),2001);auto restarted=model.Tick(3002);Check(restarted.size()==1&&restarted[0].bytes[0]==2,"GIP ACK timeout must restart Hello");
    std::mt19937 random(0x61504);for(int i=0;i<50000;++i){Examples::Bytes malformed(static_cast<std::size_t>(random()%65));for(auto& ch:malformed)ch=static_cast<std::uint8_t>(random());const auto reply=model.Output(Host(malformed),4000);for(const auto& packet:reply.packets)Check(packet.bytes.size()<=64,"Oversized GIP response");++checks;}
}
}
int main(){
    try{
        KeyboardExample::Keyboard keyboard;MouseExample::Mouse mouse;Xbox360Example::Xbox360 xbox;DualShock4Example::DualShock4 ds4;
        Fields(keyboard);Fields(mouse);Fields(xbox);Fields(ds4);Gip();
        keyboard.Set("modifiers",255);keyboard.Set("key1",4);keyboard.Set("key6",101);auto key=keyboard.Input().front().bytes;
        Check(key==Examples::Bytes({255,0,4,0,0,0,0,101}),"Keyboard modifier, reserved byte and six usages");
        mouse.Set("buttons",31);mouse.Set("x",-32768);mouse.Set("y",32767);mouse.Set("wheel",-127);mouse.Set("pan",127);
        auto motion=mouse.Input().front().bytes;Check(motion==Examples::Bytes({31,0,128,255,127,129,127}),"Mouse report encoding");
        Check(mouse.Value("x")==0&&mouse.Value("y")==0&&mouse.Value("buttons")==31,"Relative motion repeats unexpectedly");
        std::mt19937 random(0x3604);for(int i=0;i<100000;++i){
            const int x=static_cast<std::int16_t>(random()),y=static_cast<std::int16_t>(random());const int buttons=int(random()&65535),trigger=int(random()&255);
            xbox.Set("lx",x);xbox.Set("ry",y);xbox.Set("buttons",buttons);xbox.Set("lt",trigger);const auto data=xbox.Input().front().bytes;
            Check(data.size()==20&&data[0]==0&&data[1]==20&&Word(data,2)==buttons&&Word(data,6)==static_cast<std::uint16_t>(x)&&Word(data,12)==static_cast<std::uint16_t>(y)&&data[4]==trigger,"Xbox 360 report packing");
            ds4.Set("buttons",buttons&16383);ds4.Set("hat",i%9);ds4.Set("lx",trigger);ds4.Set("gyro_x",x);ds4.Set("touch1",1);ds4.Set("touch1_x",i%1920);ds4.Set("touch1_y",i%942);
            const auto report=ds4.Input().front().bytes;const int bits=(report[5]>>4)|(int(report[6])<<4)|(int(report[7]&3)<<12);
            Check(report.size()==64&&report[0]==1&&bits==(buttons&16383)&&(report[5]&15)==i%9&&report[1]==trigger&&Word(report,13)==static_cast<std::uint16_t>(x),"DS4 state packing");
            Check((report[36]|((report[37]&15)<<8))==i%1920&&((report[37]>>4)|(report[38]<<4))==i%942&&(report[35]&128)==0&&(report[39]&128)!=0,"DS4 touch packing");
        }
        std::cout<<"PASS: "<<checks<<" example protocol checks; no driver opened or device created.\n";return 0;
    }catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
