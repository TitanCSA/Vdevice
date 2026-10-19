#include "Vdevice.h"
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

// All descriptor/report construction below belongs to this example.
// The SDK provides raw transport only and does not choose a device model.
namespace {
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
Vdevice::Device_Config Make_Config() {
    Vdevice::Device_Config config;
    Vdevice::Descriptor device;
    // USB 2.0, class on interface, 64-byte EP0, VID FFFF, PID 0010, revision 1.00.
    // Strings 1/2/3 are manufacturer/product/serial; exactly one configuration.
    device.data = {18,1,0x00,0x02,0,0,0,64,0xFF,0xFF,16,0,0,1,1,2,3,1};
    Vdevice::Descriptor report;
    report.kind = Vdevice::Descriptor_Kind::Report;
    // Vendor-defined input, output and feature reports: 16 bytes each, no ID.
    report.data = {
        0x06,0x00,0xFF,0x09,0x01,0xA1,0x01,
        0x15,0x00,0x26,0xFF,0x00,0x75,0x08,0x95,0x10,
        0x09,0x02,0x81,0x02,0x09,0x03,0x91,0x02,0x09,0x04,0xB1,0x02,0xC0
    };
    Vdevice::Descriptor configuration;
    configuration.data = {
        9,2,41,0,1,1,0,0x80,50, // 41 total bytes, 100 mA, bus powered.
        9,4,0,0,2,3,0,0,0, // Interface 0, HID class.
        9,0x21,0x11,0x01,0,1,0x22,
        static_cast<std::uint8_t>(report.data.size()),
        static_cast<std::uint8_t>(report.data.size() >> 8),
        7,5,0x81,3,16,0,1 // Interrupt IN, 1 ms descriptor interval.
        ,7,5,0x01,3,16,0,1 // Interrupt OUT.
    };
    Vdevice::Descriptor languages; languages.data = {4,3,0x09,0x04};
    config.descriptors = {
        std::move(device),std::move(configuration),std::move(report),std::move(languages),
        Text(1,u"Vdevice examples"),Text(2,u"Raw vendor HID"),Text(3,u"raw-custom-01")
    };
    // Match vendor IN request 0x5A, wValue=0, wIndex=0; accept any wLength.
    Vdevice::Control_Rule version;
    version.setup = {0xC0,0x5A,0,0,0,0,0,0};
    version.mask = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0,0};
    version.data = {0x56,0x44,1,0};
    config.rules.push_back(std::move(version));
    config.delegate_controls = true;
    return config;
}

bool Pump_Output(std::uint32_t timeout) {
    const auto output = Vdevice::Get_Output(timeout);
    if (output.timed_out) return true;
    if (!output) { std::cerr << output.message << '\n'; return false; }
    if (output.kind == Vdevice::Output_Kind::Data) {
        std::cout << "Host OUT: endpoint=0x" << std::hex << unsigned(output.endpoint)
            << std::dec << ", bytes=" << output.data.size() << '\n';
    }
    if (output.request_id) {
        // HID GET_REPORT(feature, ID 0, interface 0) is this sample's protocol.
        const bool feature = output.setup[0] == 0xA1 && output.setup[1] == 1 &&
            output.setup[2] == 0 && output.setup[3] == 3 &&
            output.setup[4] == 0 && output.setup[5] == 0;
        std::vector<std::uint8_t> reply;
        if (feature) { reply.resize(16); reply[0] = 0x56; reply[1] = 0x44; reply[2] = 1; }
        // Unknown requests stall; do not acknowledge nonzero SET_IDLE without
        // implementing its periodic-report contract.
        const auto result = Vdevice::Respond_Output(output, reply, !feature);
        if (!result && result.error != 1168) {
            std::cerr << result.message << '\n'; return false;
        }
    }
    return true;
}

bool Wait_Ready(const Vdevice::Device& device) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto info = Vdevice::Get_Info();
        if (!info) { std::cerr << info.message << '\n'; return false; }
        if (info.trial_expired || info.quiescing || info.removing) {
            std::cerr << "The controller is stopping or the trial expired.\n"; return false;
        }
        for (const auto& current : info.devices)
            if (current.token == device.token && current.configured) return true;
        // Keep servicing requests during USB enumeration, with a bounded wait.
        if (!Pump_Output(25)) return false;
    }
    std::cerr << "Windows did not configure the device within ten seconds.\n";
    return false;
}
}
int main(int argc, char** argv) {
    const auto config = Make_Config();
    if (argc > 1) {
        const std::string argument = argv[1];
        if (argument == "--help") {
            std::cout << "Custom: raw vendor HID input/output/feature reports, static vendor reply and delegated controls.\n"
                "--validate checks only descriptor/rule bytes offline, without opening the driver.\n"
                "For the live example, install Vdevice and run this program elevated.\n";
            return 0;
        }
        if (argument == "--validate") {
            const auto result = Vdevice::Validate_Device(config);
            if (!result) { std::cerr << result.message << '\n'; return 1; }
            std::cout << "Custom: raw descriptor configuration valid; no driver opened.\n";
            return 0;
        }
        std::cerr << "Use --help, --validate, or no arguments for the live example.\n";
        return 2;
    }
    const auto valid = Vdevice::Validate_Device(config);
    if (!valid) { std::cerr << valid.message << '\n'; return 1; }

    auto device = Vdevice::Create_Device(config);
    if (!device) { std::cerr << device.message << '\n'; return 1; }
    if (!Wait_Ready(device)) return 1;
    std::vector<std::uint8_t> bytes(16, 0); bytes[0] = 42;
    auto result = Vdevice::Send_Input(device, 0x81, bytes);
    if (!result) { std::cerr << result.message << '\n'; return 1; }
    std::cout << "Use a HID client to write 16 bytes or request the 16-byte feature report.\n";
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < until)
        if (!Pump_Output(100)) return 1;
    // Neutral is defined by this sample's own protocol, not by the library.
    result = Vdevice::Send_Input(device, 0x81, std::vector<std::uint8_t>(16, 0));
    if (!result) { std::cerr << result.message << '\n'; return 1; }
    const auto removed = Vdevice::Delete_Device(device);
    if (!removed) { std::cerr << removed.message << '\n'; return 1; }
    std::cout << "Device disconnected.\n";
    return 0;
}
