#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsvc.h>
#include "ExampleDevice.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <mutex>
#include <thread>

namespace Examples {
inline std::uint64_t Now(){return GetTickCount64();}
inline std::string InstallationError(DWORD error){
    if(error==ERROR_SERVICE_DOES_NOT_EXIST)return "Vdevice is not installed. Run VdeviceSetup.exe or VdeviceSetupConsole.exe /install, then launch the example again.";
    return "Could not verify Vdevice installation (Windows error "+std::to_string(error)+").";
}
inline void CheckInstallation(){
    const auto manager=OpenSCManagerW(nullptr,nullptr,SC_MANAGER_CONNECT);
    if(!manager)throw std::runtime_error(InstallationError(GetLastError()));
    const auto service=OpenServiceW(manager,L"Vdevice",SERVICE_QUERY_STATUS);const DWORD error=service?0:GetLastError();
    if(service)CloseServiceHandle(service);CloseServiceHandle(manager);
    if(error)throw std::runtime_error(InstallationError(error));
}
inline void Require(const Vdevice::Result& result){
    if(!result)throw std::runtime_error(result.message+" (Windows error "+std::to_string(result.error)+")");
}
class Session {
    Protocol& protocol;
    Vdevice::Device device;
    mutable std::mutex gate;
    std::thread worker;
    std::atomic<bool> stopping{false},ready{false},opened{false};
    std::function<void(const std::string&)> logger;
    bool configured=false;
    void Write(const std::vector<Packet>& packets){
        for(const auto& packet:packets){
            auto result=Vdevice::Send_Input(device,packet.endpoint,packet.bytes);
            // No delay on the normal path. Only actual queue backpressure retries.
            const auto deadline=Now()+250;
            while(!result && result.error==ERROR_BUSY && Now()<deadline && !stopping){Sleep(1);result=Vdevice::Send_Input(device,packet.endpoint,packet.bytes);}
            Require(result);
        }
    }
    void Work()noexcept{
        try {
            std::uint64_t nextInfo=0;
            while(!stopping){
                const auto output=Vdevice::Get_Output(50);
                if(!output && !output.timed_out){if(stopping)break;Require(output);}
                std::lock_guard<std::mutex> lock(gate);if(stopping)break;const auto now=Now();
                if(!output.timed_out && output.device_id==device.Id()){
                    auto reply=protocol.Output(output,now);
                    if(output.request_id){const auto responded=Vdevice::Respond_Output(output,reply.bytes,!reply.handled||reply.stall);if(!responded && responded.error!=ERROR_NOT_FOUND)Require(responded);}
                    Write(reply.packets);
                    if(!reply.text.empty())Log(reply.text);
                    else if(output.kind==Vdevice::Output_Kind::Data)Log("Host OUT "+std::to_string(output.endpoint)+": "+Hex(output.data));
                    else if(output.request_id)Log(std::string(reply.handled&&!reply.stall?"Replied to":"Stalled unsupported")+" control: "+Hex(Bytes(output.setup.begin(),output.setup.end())));
                    if(output.kind==Vdevice::Output_Kind::DeviceState)nextInfo=0;
                }
                if(now>=nextInfo){
                    const auto info=Vdevice::Get_Info();Require(info);
                    if(info.trial_expired)throw std::runtime_error("The shared two-hour demo allowance expired. Restart Windows to use it again.");
                    bool present=false;
                    for(const auto& current:info.devices)if(current.token==device.token){
                        present=true;
                        if(current.configured&&!configured){configured=true;Write(protocol.Start(now));Log("USB configured. Protocol initialization started.");}
                        else if(!current.configured&&configured){configured=false;ready=false;Log("USB configuration suspended or removed.");}
                    }
                    if(!present)throw std::runtime_error("The virtual device was removed by Windows.");
                    nextInfo=now+(configured?1000:100);
                }
                if(configured){Write(protocol.Tick(now));const bool active=protocol.Ready();if(active&&!ready)Log("Ready: input reports can now be sent.");ready=active;}
            }
        }catch(const std::exception& error){ready=false;Log(std::string("Device error: ")+error.what());}
    }
public:
    Session(Protocol& model,std::function<void(const std::string&)> log):protocol(model),logger(std::move(log)){}
    ~Session(){Close();}
    Session(const Session&)=delete;Session& operator=(const Session&)=delete;
    void Log(const std::string& text)const noexcept{try{logger(text);}catch(...) {}}
    bool IsOpen()const noexcept{return opened;}
    bool IsReady()const noexcept{return ready;}
    void Open(){
        CheckInstallation();std::lock_guard<std::mutex> lock(gate);
        if(opened)throw std::runtime_error("A device is already created by this example.");
        LARGE_INTEGER counter{};QueryPerformanceCounter(&counter);
        const auto serial="vd-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(counter.QuadPart);
        protocol.Reset();auto config=protocol.Config(serial);Require(Vdevice::Validate_Device(config));
        device=Vdevice::Create_Device(config);
        if(!device)throw std::runtime_error(device.message+" (Windows error "+std::to_string(device.error)+"). Install Vdevice and run this example as administrator.");
        stopping=false;configured=false;ready=false;opened=true;
        try{worker=std::thread([this]{Work();});}catch(...){opened=false;Vdevice::Delete_Device(device);throw;}
        Log("Created "+device.token+". Waiting for Windows enumeration; no input is sent before configuration.");
    }
    void Close()noexcept{
        stopping=true;if(worker.joinable())worker.join();
        std::lock_guard<std::mutex> lock(gate);
        if(device){
            try{if(configured&&protocol.Ready()){protocol.Reset();Write(protocol.Input());}}catch(...){}
            const auto result=Vdevice::Delete_Device(device);if(!result)Log(result.message);
            device=Vdevice::Device{};Log("Disconnected. Virtual keys and buttons are released with device removal.");
        }
        ready=false;opened=false;configured=false;
    }
    std::vector<Field> Fields()const{std::lock_guard<std::mutex> lock(gate);return protocol.fields;}
    void Apply(const std::vector<std::pair<std::string,int>>& changes,bool neutral=false){
        std::lock_guard<std::mutex> lock(gate);
        if(!ready)throw std::runtime_error("Device is not ready. Create it and wait for Ready in the log.");
        const auto saved=protocol.fields;
        try{if(neutral)protocol.Reset();for(const auto& change:changes)protocol.Set(change.first,change.second);Write(protocol.Input());}
        catch(...){protocol.fields=saved;throw;}
    }
    void Raw(std::uint8_t endpoint,const Bytes& data){
        std::lock_guard<std::mutex> lock(gate);if(!device)throw std::runtime_error("Create a device first.");Write({{endpoint,data}});
    }
    std::string Info(){
        std::lock_guard<std::mutex> lock(gate);const auto info=Vdevice::Get_Info();Require(info);
        std::ostringstream text;text<<"Demo remaining: "<<(info.trial_remaining_100ns/10000000)<<" seconds; expired="<<info.trial_expired;
        for(const auto& current:info.devices)if(current.token==device.token)text<<"\n"<<current.token<<" configured="<<current.configured<<" accepted="<<current.inputs_accepted<<" completed="<<current.inputs_completed<<" pending="<<current.queued_inputs<<" OUT="<<current.outputs_received<<" dropped="<<current.outputs_dropped;
        return text.str();
    }
};
}
