#include "ExampleDevice.h"

namespace KeyboardExample {
Vdevice::Descriptor Text(std::uint32_t index, const std::u16string& text) {
    Vdevice::Descriptor result;
    result.index = index; result.language = 0x0409;
    result.data = {static_cast<std::uint8_t>(2 + 2 * text.size()), 3};
    for (const auto unit : text) {
        result.data.push_back(static_cast<std::uint8_t>(unit));
        result.data.push_back(static_cast<std::uint8_t>(unit >> 8));
    }
    return result;
}
Vdevice::Device_Config Make_Config(const std::string& serial) {
    Vdevice::Device_Config config;
    Vdevice::Descriptor device;
    // USB 2.0, class on interface, 64-byte EP0, VID FFFF, PID 0001, revision 1.00.
    // Strings 1/2/3 are manufacturer/product/serial; exactly one configuration.
    device.data = {18,1,0x00,0x02,0,0,0,64,0xFF,0xFF,1,0,0,1,1,2,3,1};
    Vdevice::Descriptor report;
    report.kind = Vdevice::Descriptor_Kind::Report;
    // 8 modifier bits, one reserved byte, six key usages; 5 LED output bits.
    report.data = {
        0x05,0x01,0x09,0x06,0xA1,0x01,
        0x05,0x07,0x19,0xE0,0x29,0xE7,0x15,0x00,0x25,0x01,
        0x75,0x01,0x95,0x08,0x81,0x02,
        0x95,0x01,0x75,0x08,0x81,0x01,
        0x95,0x05,0x75,0x01,0x05,0x08,0x19,0x01,0x29,0x05,0x91,0x02,
        0x95,0x01,0x75,0x03,0x91,0x01,
        0x95,0x06,0x75,0x08,0x15,0x00,0x25,0x65,0x05,0x07,
        0x19,0x00,0x29,0x65,0x81,0x00,0xC0
    };
    Vdevice::Descriptor configuration;
    configuration.data = {
        9,2,41,0,1,1,0,0x80,50, // 41 total bytes, 100 mA, bus powered.
        9,4,0,0,2,3,1,1,0, // Interface 0, HID class.
        9,0x21,0x11,0x01,0,1,0x22,
        static_cast<std::uint8_t>(report.data.size()),
        static_cast<std::uint8_t>(report.data.size() >> 8),
        7,5,0x81,3,8,0,1 // Interrupt IN, 1 ms descriptor interval.
        ,7,5,0x01,3,1,0,1 // Interrupt OUT.
    };
    Vdevice::Descriptor languages; languages.data = {4,3,0x09,0x04};
    config.descriptors = {
        std::move(device),std::move(configuration),std::move(report),std::move(languages),
        Text(1,u"Vdevice examples"),Text(2,u"Raw USB keyboard"),Examples::String(3,serial)
    };
    return config;
}
class Keyboard final:public Examples::Protocol {
public:
    Keyboard(){
        title="Virtual USB keyboard";description="Boot HID keyboard: modifiers and six simultaneous USB key usages.";
        legend="USB usages: A=4, B=5, ... Z=29; Enter=40, Escape=41, Backspace=42, Tab=43, Space=44. Modifiers: Ctrl=1, Shift=2, Alt=4, Win=8 (right keys use bits 4..7).";
        Add("modifiers",0,255);button_field="modifiers";
        buttons={{"Left Ctrl",1},{"Left Shift",2},{"Left Alt",4},{"Left Win",8},{"Right Ctrl",16},{"Right Shift",32},{"Right Alt",64},{"Right Win",128}};
        Add("key1",0,101);Add("key2",0,101);Add("key3",0,101);Add("key4",0,101);Add("key5",0,101);Add("key6",0,101);
    }
    Vdevice::Device_Config Config(const std::string& serial)const override{return Make_Config(serial);}
    std::vector<Examples::Packet> Input()override{
        Examples::Bytes data(8,0);data[0]=static_cast<std::uint8_t>(Value("modifiers"));
        for(std::size_t i=1;i<fields.size();++i)data[i+1]=static_cast<std::uint8_t>(fields[i].value);
        return {{0x81,std::move(data)}};
    }
    Examples::Reply Output(const Vdevice::Output& output,std::uint64_t now)override{
        auto reply=Protocol::Output(output,now);
        if(output.kind==Vdevice::Output_Kind::Data&&!output.data.empty())reply.text="Keyboard LEDs (Num/Caps/Scroll): "+std::to_string(output.data[0]&7);
        return reply;
    }
};
}
#ifndef VDEVICE_EXAMPLE_TEST
#define VDEVICE_EXAMPLE_MODEL KeyboardExample::Keyboard
#include "ExampleEntry.h"
#endif
