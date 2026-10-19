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
    // USB 2.0, class on interface, 64-byte EP0, VID FFFF, PID 0003, revision 1.00.
    // Strings 1/2/3 are manufacturer/product/serial; exactly one configuration.
    device.data = {18,1,0x00,0x02,0,0,0,64,0xFF,0xFF,3,0,0,1,1,2,3,1};
    Vdevice::Descriptor report;
    report.kind = Vdevice::Descriptor_Kind::Report;
    // Two X/Y pairs in separate physical collections, then Z/Rz.
    // 32 button bits and two 4-bit hats follow the six signed 16-bit axes.
    report.data = {
        0x05,0x01,0x09,0x05,0xA1,0x01,
        0x09,0x01,0xA1,0x00,0x09,0x30,0x09,0x31,
        0x16,0x00,0x80,0x26,0xFF,0x7F,0x75,0x10,0x95,0x02,0x81,0x02,0xC0,
        0x09,0x01,0xA1,0x00,0x09,0x30,0x09,0x31,
        0x75,0x10,0x95,0x02,0x81,0x02,0xC0,
        0x09,0x32,0x09,0x35,0x75,0x10,0x95,0x02,0x81,0x02,
        0x05,0x09,0x19,0x01,0x29,0x20,0x15,0x00,0x25,0x01,
        0x75,0x01,0x95,0x20,0x81,0x02,
        0x05,0x01,0x09,0x39,0x09,0x39,0x15,0x00,0x25,0x07,
        0x35,0x00,0x46,0x3B,0x01,0x65,0x14,0x75,0x04,0x95,0x02,0x81,0x42,
        0xC0
    };
    Vdevice::Descriptor configuration;
    configuration.data = {
        9,2,34,0,1,1,0,0x80,50, // 34 total bytes, 100 mA, bus powered.
        9,4,0,0,1,3,0,0,0, // Interface 0, HID class.
        9,0x21,0x11,0x01,0,1,0x22,
        static_cast<std::uint8_t>(report.data.size()),
        static_cast<std::uint8_t>(report.data.size() >> 8),
        7,5,0x81,3,17,0,1 // Interrupt IN, 1 ms descriptor interval.
    };
    Vdevice::Descriptor languages; languages.data = {4,3,0x09,0x04};
    config.descriptors = {
        std::move(device),std::move(configuration),std::move(report),std::move(languages),
        Text(1,u"Vdevice examples"),Text(2,u"Raw USB 6-axis gamepad"),Text(3,u"raw-gamepad-01")
    };
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
        // This example has no additional class/vendor protocol to implement.
        // Stalling an unknown request is different from accepting it.
        const auto result = Vdevice::Respond_Output(output, {}, true);
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
            std::cout << "Gamepad: raw 17-byte reports; two separate X/Y pairs, Z/Rz, 32 buttons and two hats.\n"
                "--validate checks only descriptor/rule bytes offline, without opening the driver.\n"
                "For the live example, install Vdevice and run this program elevated.\n";
            return 0;
        }
        if (argument == "--validate") {
            const auto result = Vdevice::Validate_Device(config);
            if (!result) { std::cerr << result.message << '\n'; return 1; }
            std::cout << "Gamepad: raw descriptor configuration valid; no driver opened.\n";
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
    std::vector<std::uint8_t> neutral(17, 0);
    neutral[16] = 0x88; // Both hats use 8 (null) for released in this descriptor.
    auto result = Vdevice::Send_Input(device, 0x81, neutral);
    if (!result) { std::cerr << result.message << '\n'; return 1; }
    std::cout << "Open joy.cpl to inspect this HID gamepad, then press Enter.\n";
    std::cin.get();
    auto active = neutral;
    active[0] = 0x80; active[1] = 0x3E; // First X = 16000, little endian.
    active[2] = 0x80; active[3] = 0xC1; // First Y = -16000.
    active[12] = 1;                   // Button 1.
    active[16] = 0x82;                // Hat 1=right, hat 2=released.
    result = Vdevice::Send_Input(device, 0x81, active);
    if (!result) { std::cerr << result.message << '\n'; return 1; }
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < until)
        if (!Pump_Output(50)) return 1;
    result = Vdevice::Send_Input(device, 0x81, neutral);
    if (!result) { std::cerr << result.message << '\n'; return 1; }
    std::cout << "Controls released. Press Enter to remove the device.\n";
    std::cin.get();
    const auto removed = Vdevice::Delete_Device(device);
    if (!removed) { std::cerr << removed.message << '\n'; return 1; }
    std::cout << "Device disconnected.\n";
    return 0;
}
