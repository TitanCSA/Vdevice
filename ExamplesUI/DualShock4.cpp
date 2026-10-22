#include "ExampleDevice.h"
#include "ViGEmDescriptors.h"

namespace DualShock4Example {
class DualShock4 final:public Examples::Protocol {
    std::uint8_t sequence=0;
public:
    DualShock4(){
        title="DualShock 4 / DS4";description="ViGEm-compatible USB DS4 reports, including motion and two touch contacts.";
        legend="Sticks: 0..255, center 128. Hat: 0=up, clockwise through 7; 8=released. Touch coordinates: 1920 x 942. Rumble and RGB light output appear in the log.";
        Add("buttons",0,16383);Add("hat",0,8,8);
        Add("lx",0,255,128);Add("ly",0,255,128);Add("rx",0,255,128);Add("ry",0,255,128);Add("lt",0,255);Add("rt",0,255);
        Add("gyro_x",-32768,32767);Add("gyro_y",-32768,32767);Add("gyro_z",-32768,32767);
        Add("accel_x",-32768,32767);Add("accel_y",-32768,32767);Add("accel_z",-32768,32767,8192);
        Add("touch1",0,1);Add("touch1_x",0,1919);Add("touch1_y",0,941);
        Add("touch2",0,1);Add("touch2_x",0,1919);Add("touch2_y",0,941);
        buttons={{"Square",1},{"Cross",2},{"Circle",4},{"Triangle",8},{"L1",16},{"R1",32},{"L2 button",64},{"R2 button",128},{"Share",256},{"Options",512},{"L3",1024},{"R3",2048},{"PS",4096},{"Touch click",8192}};
    }
    Vdevice::Device_Config Config(const std::string& serial)const override{
        using namespace Examples;Vdevice::Device_Config config;config.delegate_controls=true;
        config.descriptors={Descriptor({18,1,0,2,0,0,0,64,0x4C,5,0xC4,5,0,1,1,2,0,1}),
            Descriptor(Bytes(std::begin(ExampleDescriptors::Ds4Configuration),std::end(ExampleDescriptors::Ds4Configuration))),
            Descriptor(Bytes(std::begin(ExampleDescriptors::Ds4Report),std::end(ExampleDescriptors::Ds4Report)),Vdevice::Descriptor_Kind::Report)};
        Strings(config,"Sony Computer Entertainment","Wireless Controller","");
        // The USB feature replies exposed by ViGEm's DS4 target. The address is
        // synthetic and is not the identity of a real Bluetooth controller.
        config.rules.push_back(Rule(0xA1,1,0x312,0,{0x12,0x01,0x00,0xDA,0x37,0x13,0xC0,0x08,0x25,0,0,0,0,0,0,0}));
        const auto identity=Identity(serial);std::copy(identity.begin(),identity.end(),config.rules.back().data.begin()+1);
        config.rules.push_back(Rule(0xA1,1,0x302,0,{0x02,0x01,0,0,0,0,0,0x87,0x22,0x7B,0xDD,0xB2,0x22,0x47,0xDD,0xBD,0x22,0x43,0xDD,0x1C,0x02,0x1C,0x02,0x7F,0x1E,0x2E,0xDF,0x60,0x1F,0x4C,0xE0,0x3A,0x1D,0xC6,0xDE,0x08,0}));
        config.rules.push_back(Rule(0xA1,1,0x3A3,0,{0xA3,0x41,0x75,0x67,0x20,0x20,0x33,0x20,0x32,0x30,0x31,0x33,0,0,0,0,0,0x30,0x37,0x3A,0x30,0x31,0x3A,0x31,0x32,0,0,0,0,0,0,0,0,0,1,0,0x31,3,0,0,0,0x49,0,5,0,0,0x80,3,0}));
        return config;
    }
    std::vector<Examples::Packet> Input()override{
        using namespace Examples;Bytes data(64,0);data[0]=1;
        data[1]=static_cast<std::uint8_t>(Value("lx"));data[2]=static_cast<std::uint8_t>(Value("ly"));
        data[3]=static_cast<std::uint8_t>(Value("rx"));data[4]=static_cast<std::uint8_t>(Value("ry"));
        const auto bits=Value("buttons");data[5]=static_cast<std::uint8_t>((bits<<4)|Value("hat"));
        data[6]=static_cast<std::uint8_t>(bits>>4);data[7]=static_cast<std::uint8_t>(((bits>>12)&3)|((sequence++&63)<<2));
        data[8]=static_cast<std::uint8_t>(Value("lt"));data[9]=static_cast<std::uint8_t>(Value("rt"));
        Put16(data,10,int(sequence)*188);data[12]=20;Put16(data,13,Value("gyro_x"));Put16(data,15,Value("gyro_y"));Put16(data,17,Value("gyro_z"));
        Put16(data,19,Value("accel_x"));Put16(data,21,Value("accel_y"));Put16(data,23,Value("accel_z"));data[30]=0x1B;
        data[33]=1;data[34]=sequence;
        for(int contact=0;contact<2;++contact){
            const std::string prefix=contact?"touch2":"touch1";const auto offset=35+std::size_t(contact)*4;
            const int x=Value(prefix+"_x"),y=Value(prefix+"_y");
            data[offset]=static_cast<std::uint8_t>(contact|(Value(prefix)?0:0x80));
            data[offset+1]=static_cast<std::uint8_t>(x);data[offset+2]=static_cast<std::uint8_t>((x>>8)|((y&15)<<4));data[offset+3]=static_cast<std::uint8_t>(y>>4);
        }
        return {{0x84,std::move(data)}};
    }
    Examples::Reply Output(const Vdevice::Output& output,std::uint64_t now)override{
        auto reply=Protocol::Output(output,now);
        if(output.kind==Vdevice::Output_Kind::Data && output.data.size()>=9 && output.data[0]==5)
            reply.text="DS4 output: small="+std::to_string(output.data[4])+", large="+std::to_string(output.data[5])+", RGB="+std::to_string(output.data[6])+","+std::to_string(output.data[7])+","+std::to_string(output.data[8]);
        return reply;
    }
};
}
#ifndef VDEVICE_EXAMPLE_TEST
#define VDEVICE_EXAMPLE_MODEL DualShock4Example::DualShock4
#include "ExampleEntry.h"
#endif
