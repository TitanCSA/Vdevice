#include "ExampleDevice.h"

namespace MouseExample {
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
    // USB 2.0, class on interface, 64-byte EP0, VID FFFF, PID 0002, revision 1.00.
    // Strings 1/2/3 are manufacturer/product/serial; exactly one configuration.
    device.data = {18,1,0x00,0x02,0,0,0,64,0xFF,0xFF,2,0,0,1,1,2,3,1};
    Vdevice::Descriptor report;
    report.kind = Vdevice::Descriptor_Kind::Report;
    // Five buttons + padding; signed 16-bit X/Y; signed 8-bit wheel and AC Pan.
    report.data = {
        0x05,0x01,0x09,0x02,0xA1,0x01,0x09,0x01,0xA1,0x00,
        0x05,0x09,0x19,0x01,0x29,0x05,0x15,0x00,0x25,0x01,
        0x95,0x05,0x75,0x01,0x81,0x02,
        0x95,0x01,0x75,0x03,0x81,0x01,
        0x05,0x01,0x09,0x30,0x09,0x31,0x16,0x00,0x80,0x26,0xFF,0x7F,
        0x75,0x10,0x95,0x02,0x81,0x06,
        0x09,0x38,0x15,0x81,0x25,0x7F,0x75,0x08,0x95,0x01,0x81,0x06,
        0x05,0x0C,0x0A,0x38,0x02,0x95,0x01,0x81,0x06,0xC0,0xC0
    };
    Vdevice::Descriptor configuration;
    configuration.data = {
        9,2,34,0,1,1,0,0x80,50, // 34 total bytes, 100 mA, bus powered.
        9,4,0,0,1,3,0,0,0, // Interface 0, HID class.
        9,0x21,0x11,0x01,0,1,0x22,
        static_cast<std::uint8_t>(report.data.size()),
        static_cast<std::uint8_t>(report.data.size() >> 8),
        7,5,0x81,3,7,0,1 // Interrupt IN, 1 ms descriptor interval.
    };
    Vdevice::Descriptor languages; languages.data = {4,3,0x09,0x04};
    config.descriptors = {
        std::move(device),std::move(configuration),std::move(report),std::move(languages),
        Text(1,u"Vdevice examples"),Text(2,u"Raw USB mouse"),Examples::String(3,serial)
    };
    return config;
}
class Mouse final:public Examples::Protocol {
public:
    Mouse(){
        title="Virtual USB mouse";description="Five buttons, signed 16-bit relative motion and vertical/horizontal wheels.";
        legend="X: right positive. Y: down positive. Wheel/pan are signed HID units. Motion resets after each send; button state stays held.";
        relative=true;Add("buttons",0,31);Add("x",-32768,32767);Add("y",-32768,32767);Add("wheel",-127,127);Add("pan",-127,127);
        buttons={{"Left",1},{"Right",2},{"Middle",4},{"Back",8},{"Forward",16}};
    }
    Vdevice::Device_Config Config(const std::string& serial)const override{return Make_Config(serial);}
    std::vector<Examples::Packet> Input()override{
        Examples::Bytes data(7,0);data[0]=static_cast<std::uint8_t>(Value("buttons"));
        Examples::Put16(data,1,Value("x"));Examples::Put16(data,3,Value("y"));
        data[5]=static_cast<std::uint8_t>(Value("wheel"));data[6]=static_cast<std::uint8_t>(Value("pan"));
        for(auto& field:fields)if(std::string(field.name)!="buttons")field.value=0;
        return {{0x81,std::move(data)}};
    }
};
}
#ifndef VDEVICE_EXAMPLE_TEST
#define VDEVICE_EXAMPLE_MODEL MouseExample::Mouse
#include "ExampleEntry.h"
#endif
