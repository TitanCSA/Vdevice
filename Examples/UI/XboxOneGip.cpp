#include "ExampleDevice.h"
#include "GipMetadata.h"

namespace XboxOneGipExample {
class XboxOneGip final:public Examples::Protocol {
    enum class Phase {Arrival,FirstAck,LastAck,Idle,Active,Off};
    Phase phase=Phase::Arrival;
    std::uint8_t commandSequence=0,inputSequence=0,metadataSequence=0;
    std::uint64_t nextHello=0,deadline=0,nextStatus=0,activeSince=0;
    std::uint64_t replayUntil=0,nextReplay=0;
    bool guide=false,usbSecurityReady=false;
    bool replayRequested=false;
    mutable std::array<std::uint8_t,6> identity=Examples::Identity("Vdevice-GIP");
    static std::uint8_t Next(std::uint8_t& sequence){if(++sequence==0)++sequence;return sequence;}
    Examples::Packet Message(std::uint8_t type,Examples::Bytes bytes){
        Examples::Bytes packet={type,0x20,Next(commandSequence),static_cast<std::uint8_t>(bytes.size())};
        packet.insert(packet.end(),bytes.begin(),bytes.end());return {0x81,std::move(packet)};
    }
    Examples::Packet Hello(){
        // 8-byte DeviceID (top 16 bits zero), VID/PID, firmware 1.1.0.1,
        // hardware 1.0, RF/security/GIP protocol versions 1.0.
        auto packet=Message(2,{1,0xDA,0x37,0x13,0xC0,1,0,0,0xFF,0xFF,4,0,1,0,1,0,0,0,1,0,1,0,1,0,1,0,1,0});
        std::copy(identity.begin(),identity.end(),packet.bytes.begin()+4);return packet;
    }
    Examples::Packet Fragment(std::size_t offset,std::size_t length,std::uint8_t flags)const{
        const auto position=offset?offset:sizeof(ExampleDescriptors::GipMetadata);
        Examples::Bytes data={4,flags,metadataSequence,static_cast<std::uint8_t>(length),
            static_cast<std::uint8_t>((position&127)|128),static_cast<std::uint8_t>(position>>7)};
        data.insert(data.end(),ExampleDescriptors::GipMetadata+offset,ExampleDescriptors::GipMetadata+offset+length);
        return {0x81,std::move(data)};
    }
    std::vector<Examples::Packet> Activate(std::uint64_t now){
        phase=Phase::Active;nextStatus=now+1000;activeSince=now;
        auto packets=Input();packets.insert(packets.begin(),Message(3,{0x80,0,0,0}));return packets;
    }
    static bool VarInt(const Examples::Bytes& bytes,std::size_t& offset,std::size_t& value){
        value=0;for(unsigned shift=0;shift<21;shift+=7){if(offset>=bytes.size())return false;const auto ch=bytes[offset++];value|=std::size_t(ch&127)<<shift;if(!(ch&128))return true;}return false;
    }
public:
    XboxOneGip(){
        title="Xbox One / native GIP";description="USB GIP gamepad with Hello, reliable metadata transfer, state, Guide and rumble messages.";
        legend="Requires Windows 10 21H2 or later. XInput button bits are translated to GIP bytes by this example. Triggers: 0..1023. Guide is a separate virtual-key message. No console authentication, audio or accessory emulation.";
        Add("buttons",0,65535);Add("lx",-32768,32767);Add("ly",-32768,32767);Add("rx",-32768,32767);Add("ry",-32768,32767);Add("lt",0,1023);Add("rt",0,1023);Add("guide",0,1);
        buttons={{"Up",1},{"Down",2},{"Left",4},{"Right",8},{"Menu",16},{"View",32},{"L stick",64},{"R stick",128},{"LB",256},{"RB",512},{"A",4096},{"B",8192},{"X",16384},{"Y",32768}};
    }
    Vdevice::Device_Config Config(const std::string& serial)const override{
        using namespace Examples;identity=Identity(serial);Vdevice::Device_Config config;config.delegate_controls=true;
        config.descriptors={Descriptor({18,1,0,2,255,0x47,0xD0,64,0xFF,0xFF,4,0,0,1,1,2,3,1}),
            Descriptor({9,2,32,0,1,1,0,0xA0,250,9,4,0,0,2,255,0x47,0xD0,0,7,5,0x81,3,64,0,4,7,5,1,3,64,0,4})};
        Strings(config,"Vdevice / Microsoft-compatible","Xbox One GIP Gamepad",serial);CompatibleId(config,"XGIP10");return config;
    }
    bool Ready()const override{return phase==Phase::Active&&usbSecurityReady;}
    std::vector<Examples::Packet> Start(std::uint64_t now)override{
        phase=Phase::Arrival;commandSequence=inputSequence=0;guide=false;usbSecurityReady=false;replayRequested=false;replayUntil=nextReplay=0;nextHello=now+500;return {Hello()};
    }
    std::vector<Examples::Packet> Input()override{
        replayRequested=true;return BuildInput();
    }
    std::vector<Examples::Packet> BuildInput(){
        if(phase!=Phase::Active)throw std::runtime_error("Windows has not started the GIP protocol yet. Wait for Ready in the log.");
        using namespace Examples;const auto buttonsValue=Value("buttons");
        const auto bits=((buttonsValue&15)<<8)|((buttonsValue&0x30)>>2)|((buttonsValue&0xC0)<<8)|((buttonsValue&0x300)<<4)|((buttonsValue&0xF000)>>8);
        Bytes data(18,0);data[0]=0x20;data[2]=Next(inputSequence);data[3]=14;Put16(data,4,bits);
        Put16(data,6,Value("lt"));Put16(data,8,Value("rt"));Put16(data,10,Value("lx"));Put16(data,12,Value("ly"));Put16(data,14,Value("rx"));Put16(data,16,Value("ry"));
        std::vector<Packet> packets={{0x81,std::move(data)}};
        const bool pressed=Value("guide")!=0;if(pressed!=guide){guide=pressed;packets.push_back(Message(7,{static_cast<std::uint8_t>(guide),0x5B}));}
        return packets;
    }
    std::vector<Examples::Packet> Tick(std::uint64_t now)override{
        if((phase==Phase::FirstAck||phase==Phase::LastAck)&&now>=deadline){phase=Phase::Arrival;nextHello=now;}
        if(phase==Phase::Arrival&&now>=nextHello){nextHello=now+500;return {Hello()};}
        if(phase==Phase::Idle&&now>=deadline)return Activate(now);
        if(Ready()&&now>=nextStatus){nextStatus=now+(now-activeSince<10000?1000:20000);return {Message(3,{0x80,0,0,0})};}
        // Windows can finish creating its XInput consumer after USB/GIP START.
        // Publish immediately, then briefly repeat the latest state so startup
        // and the tail of a burst do not depend on a second user command.
        // This stops when idle; it never delays a user's initial send.
        if(Ready()){
            if(replayRequested){replayUntil=(std::max)(now+250,activeSince+2000);replayRequested=false;}
            if(now<=replayUntil&&now>=nextReplay){nextReplay=now+25;return BuildInput();}
        }
        return {};
    }
    Examples::Reply Output(const Vdevice::Output& output,std::uint64_t now)override{
        using namespace Examples;Reply reply;
        if(output.kind!=Vdevice::Output_Kind::Data)return reply;
        const auto& data=output.data;std::size_t cursor=0;
        while(cursor<data.size()){
            if(data.size()-cursor<4){reply.text="Truncated GIP header rejected.";break;}
            const auto type=data[cursor++],flags=data[cursor++],sequence=data[cursor++];std::size_t length=0,position=0;
            if(!VarInt(data,cursor,length)||((flags&0x80)&&!VarInt(data,cursor,position))||length>data.size()-cursor){reply.text="Malformed GIP packet rejected.";break;}
            const auto begin=cursor;cursor+=length;
            if((flags&15)!=0)continue; // This example has no attached secondary device.
            if(flags&0x80){reply.text="Fragmented downstream extensions are not implemented by this gamepad example.";continue;}
            if(flags&0x10){
                Bytes ack={1,0x20,sequence,9,0,type,static_cast<std::uint8_t>(flags&0x20),static_cast<std::uint8_t>(length),static_cast<std::uint8_t>(length>>8),0,0,0,0};
                reply.packets.push_back({0x81,std::move(ack)});
            }
            if(type==4 && length==0){
                metadataSequence=Next(commandSequence);phase=Phase::FirstAck;deadline=now+1000;
                reply.packets.push_back(Fragment(0,58,0xF0));reply.text="GIP metadata requested; sending USB-only metadata with acknowledgements.";
            }else if(type==1 && length==9 && data[begin]==0 && data[begin+1]==4 && sequence==metadataSequence){
                const auto received=unsigned(data[begin+3])|(unsigned(data[begin+4])<<8);
                if(phase==Phase::FirstAck && received==58){
                    reply.packets.push_back(Fragment(58,58,0xA0));reply.packets.push_back(Fragment(116,58,0xA0));reply.packets.push_back(Fragment(174,sizeof(ExampleDescriptors::GipMetadata)-174,0xB0));phase=Phase::LastAck;deadline=now+1000;
                }else if(phase==Phase::LastAck && received==sizeof(ExampleDescriptors::GipMetadata)){
                    reply.packets.push_back(Fragment(sizeof(ExampleDescriptors::GipMetadata),0,0xA0));phase=Phase::Idle;deadline=now+500;reply.text="GIP metadata acknowledged.";
                }
            }else if(type==5 && length>=1){
                const auto state=data[begin];
                if(state==0){auto packets=Activate(now);reply.packets.insert(reply.packets.end(),packets.begin(),packets.end());reply.text="GIP active: Windows sent START.";}
                else if(state==1){phase=Phase::Off;reply.text="GIP stopped by Windows.";}
                else if(state==4){phase=Phase::Off;reply.text="GIP powered off by Windows.";}
                else if(state==7){auto packets=Start(now);reply.packets.insert(reply.packets.end(),packets.begin(),packets.end());reply.text="GIP reset; restarting Hello.";}
            }else if(type==9 && length==9)reply.text="GIP motor command: "+Hex(Bytes(data.begin()+begin,data.begin()+cursor));
            else if(type==6){
                if(length==2&&data[begin]==1&&data[begin+1]==0){usbSecurityReady=true;reply.text="Windows accepted the documented USB security opt-out; GIP input enabled.";}
                else reply.text=std::string(length==2?"Host GIP security status: ":"Unsupported GIP authentication data: ")+Hex(Bytes(data.begin()+begin,data.begin()+cursor));
            }
        }
        return reply;
    }
};
}
#ifndef VDEVICE_EXAMPLE_TEST
#define VDEVICE_EXAMPLE_MODEL XboxOneGipExample::XboxOneGip
#include "ExampleEntry.h"
#endif
