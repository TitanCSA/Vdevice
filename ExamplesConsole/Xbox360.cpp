#include "ExampleDevice.h"
#include "ViGEmDescriptors.h"

namespace Xbox360Example {
class Xbox360 final:public Examples::Protocol {
public:
    Xbox360(){
        title="Xbox 360 / XInput";description="ViGEm-compatible wired Xbox 360 USB interface, driven through Vdevice.";
        legend="Buttons use XInput bits. Four signed sticks (-32768..32767), two analog triggers (0..255). Host rumble and LED output appear in the log.";
        Add("buttons",0,65535);Add("lx",-32768,32767);Add("ly",-32768,32767);Add("rx",-32768,32767);Add("ry",-32768,32767);Add("lt",0,255);Add("rt",0,255);
        buttons={{"Up",1},{"Down",2},{"Left",4},{"Right",8},{"Start",16},{"Back",32},{"L stick",64},{"R stick",128},{"LB",256},{"RB",512},{"A",4096},{"B",8192},{"X",16384},{"Y",32768}};
    }
    Vdevice::Device_Config Config(const std::string& serial)const override{
        using namespace Examples;Vdevice::Device_Config config;config.delegate_controls=true;
        config.descriptors={Descriptor({18,1,0,2,255,255,255,8,0x5E,4,0x8E,2,0x14,1,1,2,3,1}),
            Descriptor(Bytes(std::begin(ExampleDescriptors::Xbox360Configuration),std::end(ExampleDescriptors::Xbox360Configuration)))};
        // Expose the gamepad function. ViGEm's unused voice/security interfaces
        // are not implemented by this USB example.
        auto& configuration=config.descriptors[1].data;configuration.resize(49);configuration[2]=49;configuration[4]=1;
        Strings(config,"Vdevice / Microsoft-compatible","Xbox 360 Controller",serial);
        // The fourth XUSB interface references string 4.
        config.descriptors.push_back(String(4,"Xbox Security Method 3, Version 1.00"));
        CompatibleId(config,"XUSB10");
        // The Xenon vendor initialization query used by the Windows XUSB driver.
        config.rules.push_back(Rule(0xC0,1,0x100,0,{0x31,0x3F,0xCF,0xDC}));
        return config;
    }
    std::vector<Examples::Packet> Input()override{
        Examples::Bytes data(20,0);data[1]=20;Examples::Put16(data,2,Value("buttons"));
        data[4]=static_cast<std::uint8_t>(Value("lt"));data[5]=static_cast<std::uint8_t>(Value("rt"));
        Examples::Put16(data,6,Value("lx"));Examples::Put16(data,8,Value("ly"));
        Examples::Put16(data,10,Value("rx"));Examples::Put16(data,12,Value("ry"));
        return {{0x81,std::move(data)}};
    }
    std::vector<Examples::Packet> Start(std::uint64_t)override{
        // ViGEm's six-stage power-up sequence and auxiliary capability pipe.
        std::vector<Examples::Packet> packets={{0x81,{1,3,14}},{0x81,{2,3,0}},{0x81,{3,3,3}},{0x81,{8,3,0}}};
        packets.push_back(Input().front());packets.push_back({0x81,{1,3,3}});
        return packets;
    }
    Examples::Reply Output(const Vdevice::Output& output,std::uint64_t)override{
        Examples::Reply reply;
        if(output.request_id && (output.setup[0]&0x60)==0x40 && (output.setup[0]&0x80)) {
            // Match the documented ViGEm four-byte Xenon reply, not arbitrary vendor reads.
            const auto length=unsigned(output.setup[6])|(unsigned(output.setup[7])<<8);
            if(length==4){reply.handled=true;reply.stall=false;reply.bytes={0x31,0x3F,0xCF,0xDC};}
        }
        if(output.kind==Vdevice::Output_Kind::Data && output.data.size()>=5 && output.data[0]==0 && output.data[1]==8)
            reply.text="Xbox 360 rumble: large="+std::to_string(output.data[3])+", small="+std::to_string(output.data[4]);
        return reply;
    }
};
}
#ifndef VDEVICE_EXAMPLE_TEST
#define VDEVICE_EXAMPLE_MODEL Xbox360Example::Xbox360
#include "ExampleEntry.h"
#endif
