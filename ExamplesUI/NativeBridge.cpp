#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include "../Vdevice/Vdevice.h"

// Private GUI glue: every interface uses the same seven raw SDK operations.
namespace {
constexpr std::uint32_t WorkshopMagic = 0x31574456u;
constexpr std::uint32_t MaxConfig = 65536, MaxDescriptors = 64, MaxRules = 32;
constexpr std::size_t MaxPendingControls = 256;
std::mutex gate;
std::map<std::uint64_t, Vdevice::Device> devices;
struct PendingControl { Vdevice::Output output; std::chrono::steady_clock::time_point received; };
using ControlKey = std::pair<std::uint64_t, std::uint64_t>;
std::map<ControlKey, PendingControl> pending;

void TextBytes(const char* source, std::size_t length, wchar_t* output, std::uint32_t capacity) noexcept {
    if (!output || !capacity) return;
    output[0] = L'\0'; if (!length) return;
    int count = length <= 0x7fffffffu ? MultiByteToWideChar(CP_UTF8, 0, source, static_cast<int>(length), nullptr, 0) : 0;
    if (count > 0 && static_cast<std::uint32_t>(count) < capacity) {
        MultiByteToWideChar(CP_UTF8, 0, source, static_cast<int>(length), output, count); output[count] = L'\0';
    } else wcsncpy_s(output, capacity, L"The diagnostic message exceeds the provided buffer.", _TRUNCATE);
}
void Text(const std::string& source, wchar_t* output, std::uint32_t capacity) noexcept { TextBytes(source.data(), source.size(), output, capacity); }
void Text(const char* source, wchar_t* output, std::uint32_t capacity) noexcept { TextBytes(source, source ? std::strlen(source) : 0, output, capacity); }
std::uint32_t Result(const Vdevice::Result& result, wchar_t* error, std::uint32_t capacity) {
    Text(result.message, error, capacity); return result.error;
}
std::uint32_t Fail(const char* message, std::uint32_t code, wchar_t* error, std::uint32_t capacity) noexcept {
    Text(message, error, capacity); return code;
}
std::uint32_t Exception(wchar_t* error, std::uint32_t capacity) noexcept {
    return Fail("The operation could not allocate or process its working data.", ERROR_NOT_ENOUGH_MEMORY, error, capacity);
}
Vdevice::Device* Find(std::uint64_t id) { auto item = devices.find(id); return item == devices.end() ? nullptr : &item->second; }
void PruneControls() {
    const auto now = std::chrono::steady_clock::now();
    for (auto item = pending.begin(); item != pending.end();) {
        if (now - item->second.received >= std::chrono::seconds(5)) item = pending.erase(item); else ++item;
    }
}
class Reader {
    const std::uint8_t* source_;
    std::size_t size_, offset_ = 0;
public:
    Reader(const std::uint8_t* source, std::uint32_t size) : source_(source), size_(size) {}
    bool Raw(void* destination, std::size_t count) {
        if (count > size_ - offset_) return false;
        if (count) std::memcpy(destination, source_ + offset_, count);
        offset_ += count; return true;
    }
    bool U32(std::uint32_t& value) { return Raw(&value, sizeof(value)); }
    bool Bytes(std::vector<std::uint8_t>& value, std::uint32_t count) {
        if (count > size_ - offset_) return false;
        value.assign(source_ + offset_, source_ + offset_ + count); offset_ += count; return true;
    }
    bool Finished() const noexcept { return offset_ == size_; }
};
bool Parse(const std::uint8_t* bytes, std::uint32_t length, Vdevice::Device_Config& config) {
    if (!bytes || length < 24 || length > MaxConfig) return false;
    Reader reader(bytes, length); std::uint32_t magic = 0, speed = 0, delegated = 0, descriptors = 0, rules = 0;
    if (!reader.U32(magic) || magic != WorkshopMagic || !reader.U32(speed) || speed < 1 || speed > 3
        || !reader.U32(config.input_depth) || !reader.U32(delegated) || delegated > 1
        || !reader.U32(descriptors) || descriptors > MaxDescriptors || !reader.U32(rules) || rules > MaxRules) return false;
    config.speed = static_cast<Vdevice::Speed>(speed); config.delegate_controls = delegated != 0;
    config.descriptors.reserve(descriptors); config.rules.reserve(rules);
    for (std::uint32_t i = 0; i < descriptors; ++i) {
        Vdevice::Descriptor descriptor; std::uint32_t kind = 0, count = 0;
        if (!reader.U32(kind) || (kind != 1 && kind != 2) || !reader.U32(descriptor.index)
            || !reader.U32(descriptor.language) || !reader.U32(descriptor.interface_number)
            || !reader.U32(count) || !reader.Bytes(descriptor.data, count)) return false;
        descriptor.kind = static_cast<Vdevice::Descriptor_Kind>(kind); config.descriptors.push_back(std::move(descriptor));
    }
    for (std::uint32_t i = 0; i < rules; ++i) {
        Vdevice::Control_Rule rule; std::uint32_t action = 0, count = 0;
        if (!reader.Raw(rule.setup.data(), rule.setup.size()) || !reader.Raw(rule.mask.data(), rule.mask.size())
            || !reader.U32(action) || action < 1 || action > 3 || !reader.U32(count) || !reader.Bytes(rule.data, count)) return false;
        rule.action = static_cast<Vdevice::Rule_Action>(action); config.rules.push_back(std::move(rule));
    }
    return reader.Finished();
}
}
struct WorkshopOutput {
    std::uint64_t device_id, request_id;
    std::uint32_t kind, endpoint, length, error;
    std::uint8_t setup[8], data[Vdevice::MaxTransfer];
};
static_assert(sizeof(WorkshopOutput) == 4136, "The GUI output ABI must match its C# layout.");
#define VD_EXPORT extern "C" __declspec(dllexport)
VD_EXPORT std::uint32_t __cdecl VdUiValidate(const std::uint8_t* bytes, std::uint32_t length, wchar_t* error, std::uint32_t capacity) noexcept {
    try {
        Vdevice::Device_Config config;
        if (!Parse(bytes, length, config)) return Fail("Invalid workshop configuration data.", ERROR_INVALID_DATA, error, capacity);
        return Result(Vdevice::Validate_Device(config), error, capacity);
    } catch (...) { return Exception(error, capacity); }
}
VD_EXPORT std::uint32_t __cdecl VdUiCreate(const std::uint8_t* bytes, std::uint32_t length,
    std::uint64_t* id, wchar_t* error, std::uint32_t capacity) noexcept {
    try {
        if (!id) return ERROR_INVALID_PARAMETER;
        *id = 0; Vdevice::Device_Config config;
        if (!Parse(bytes, length, config)) return Fail("Invalid workshop configuration data.", ERROR_INVALID_DATA, error, capacity);
        std::lock_guard<std::mutex> lock(gate); Vdevice::Device device = Vdevice::Create_Device(config);
        if (!device) return Fail(device.message.c_str(), device.error ? device.error : ERROR_GEN_FAILURE, error, capacity);
        const auto created = device.Id(); devices.emplace(created, std::move(device)); *id = created;
        Text("Created from the supplied descriptors. Windows may take a moment to enumerate its interfaces.", error, capacity); return ERROR_SUCCESS;
    } catch (...) { return Exception(error, capacity); }
}
VD_EXPORT std::uint32_t __cdecl VdUiDelete(std::uint64_t id, wchar_t* error, std::uint32_t capacity) noexcept {
    try {
        std::lock_guard<std::mutex> lock(gate); auto item = devices.find(id);
        if (item == devices.end()) return Fail("Unknown owned virtual device.", ERROR_NOT_FOUND, error, capacity);
        auto result = Vdevice::Delete_Device(item->second);
        const bool disconnected = result.error == ERROR_NOT_FOUND || result.error == ERROR_DEVICE_NOT_CONNECTED;
        if (result || disconnected) {
            devices.erase(item);
            for (auto request = pending.begin(); request != pending.end();) {
                if (request->first.first == id) request = pending.erase(request); else ++request;
            }
        }
        if (disconnected) { Text("The device was already disconnected; its GUI entry was removed.", error, capacity); return ERROR_SUCCESS; }
        return Result(result, error, capacity);
    } catch (...) { return Exception(error, capacity); }
}
VD_EXPORT std::uint32_t __cdecl VdUiSend(std::uint64_t id, std::uint32_t endpoint, const std::uint8_t* bytes,
    std::uint32_t length, wchar_t* error, std::uint32_t capacity) noexcept {
    try {
        std::lock_guard<std::mutex> lock(gate); auto device = Find(id);
        if (!device) return Fail("Unknown owned virtual device.", ERROR_NOT_FOUND, error, capacity);
        if (endpoint > 255 || length > Vdevice::MaxTransfer || (length && !bytes))
            return Fail("Invalid endpoint or transfer data.", ERROR_INVALID_PARAMETER, error, capacity);
        std::vector<std::uint8_t> data; if (length) data.assign(bytes, bytes + length);
        return Result(Vdevice::Send_Input(*device, static_cast<std::uint8_t>(endpoint), data), error, capacity);
    } catch (...) { return Exception(error, capacity); }
}
VD_EXPORT std::uint32_t __cdecl VdUiInfo(std::uint64_t selected, wchar_t* text, std::uint32_t capacity,
    wchar_t* detailText, std::uint32_t detailCapacity) noexcept {
    try {
        auto info = Vdevice::Get_Info(); if (!info) return Result(info, text, capacity);
        std::ostringstream summary; summary << "Driver ABI " << info.version << " | " << info.devices.size() << '/' << info.max_devices
            << " owned devices | max transfer " << info.max_transfer << " bytes";
        if (info.removing || info.quiescing) summary << " | controller stopping: finish uninstall or restart Windows";
        else if (info.trial_expired) summary << " | DEMO EXPIRED: restart Windows";
        else if (info.trial_started) summary << " | demo remaining " << info.trial_remaining_100ns / 10000000u << " seconds";
        else summary << " | demo starts with first successful device creation";
        Text(summary.str(), text, capacity); Text("Select an owned device to inspect its current driver counters.", detailText, detailCapacity);
        for (const auto& device : info.devices) if (device.id == selected) {
            std::ostringstream out; out << device.token << " | plugged=" << device.plugged << " configured=" << device.configured << " powered=" << device.powered
                << " | endpoints=" << device.endpoint_count << " | queued IN=" << device.queued_inputs << " OUT=" << device.queued_outputs
                << " | accepted=" << device.inputs_accepted << " completed=" << device.inputs_completed << " | outputs=" << device.outputs_received << " dropped=" << device.outputs_dropped;
            Text(out.str(), detailText, detailCapacity); break;
        }
        return info.removing || info.quiescing ? ERROR_NOT_READY : ERROR_SUCCESS;
    } catch (...) { return Exception(text, capacity); }
}
VD_EXPORT int __cdecl VdUiReceive(WorkshopOutput* packet, wchar_t* error, std::uint32_t capacity) noexcept {
    try {
        if (!packet) return -static_cast<int>(ERROR_INVALID_PARAMETER);
        *packet = {}; std::lock_guard<std::mutex> lock(gate); PruneControls(); auto output = Vdevice::Get_Output(0);
        if (output.timed_out) return 0;
        if (!output) { Text(output.message, error, capacity); return -static_cast<int>(output.error ? output.error : ERROR_GEN_FAILURE); }
        packet->device_id = output.device_id; packet->request_id = output.request_id;
        packet->kind = static_cast<std::uint32_t>(output.kind); packet->endpoint = output.endpoint; packet->length = static_cast<std::uint32_t>(output.data.size());
        if (output.data.size() > Vdevice::MaxTransfer) return -static_cast<int>(ERROR_INVALID_DATA);
        std::memcpy(packet->setup, output.setup.data(), output.setup.size());
        if (!output.data.empty()) std::memcpy(packet->data, output.data.data(), output.data.size());
        if (output.request_id) {
            if (pending.size() >= MaxPendingControls) {
                packet->error = ERROR_NOT_ENOUGH_MEMORY;
                Text("The GUI has too many pending controls. This request will time out; use control rules for high request rates.", error, capacity);
            } else pending.emplace(ControlKey{output.device_id, output.request_id}, PendingControl{std::move(output), std::chrono::steady_clock::now()});
        }
        return 1;
    } catch (...) { Exception(error, capacity); return -static_cast<int>(ERROR_NOT_ENOUGH_MEMORY); }
}
VD_EXPORT std::uint32_t __cdecl VdUiRespond(std::uint64_t id, std::uint64_t request, const std::uint8_t* bytes,
    std::uint32_t length, std::uint32_t stall, wchar_t* error, std::uint32_t capacity) noexcept {
    try {
        if (length > Vdevice::MaxTransfer || (length && !bytes) || stall > 1) return ERROR_INVALID_PARAMETER;
        std::lock_guard<std::mutex> lock(gate); PruneControls(); auto item = pending.find({id, request});
        if (item == pending.end()) return Fail("This request expired, was already answered, or belongs to a deleted device.", ERROR_NOT_FOUND, error, capacity);
        std::vector<std::uint8_t> data; if (length) data.assign(bytes, bytes + length);
        auto result = Vdevice::Respond_Output(item->second.output, data, stall != 0);
        if (result || result.error == ERROR_NOT_FOUND || result.error == ERROR_OPERATION_ABORTED) pending.erase(item);
        return Result(result, error, capacity);
    } catch (...) { return Exception(error, capacity); }
}
VD_EXPORT void __cdecl VdUiClose() noexcept {
    try { std::lock_guard<std::mutex> lock(gate); pending.clear(); devices.clear(); } catch (...) { }
}
