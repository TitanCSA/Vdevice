#define VDEVICE_EXAMPLE_TEST
#include "../ExamplesUI/Keyboard.cpp"
#include "../ExamplesUI/Mouse.cpp"
#include "../ExamplesUI/Xbox360.cpp"
#include "../ExamplesUI/DualShock4.cpp"
#include "../ExamplesUI/XboxOneGip.cpp"
#include "../ExamplesUI/ExampleRuntime.h"
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <Xinput.h>
#include <iostream>
#include <memory>

namespace {
void Check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void Guard(int argc,char** argv){
    Check(argc==2&&std::string(argv[1])=="--vm-live","Use --vm-live only in the owned test VM.");
    HKEY key=nullptr;Check(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"HARDWARE\\DESCRIPTION\\System\\BIOS",0,KEY_READ,&key)==ERROR_SUCCESS,"Read VM identity");
    wchar_t product[128]{};DWORD bytes=sizeof(product);const auto error=RegQueryValueExW(key,L"SystemProductName",nullptr,nullptr,reinterpret_cast<BYTE*>(product),&bytes);RegCloseKey(key);
    Check(error==ERROR_SUCCESS&&std::wstring(product)==L"Virtual Machine","Live tests are restricted to the Hyper-V guest.");
}
void Ready(Examples::Session& session){
    session.Open();const auto deadline=Examples::Now()+15000;
    while(!session.IsReady()&&Examples::Now()<deadline)Sleep(10);
    Check(session.IsReady(),"Windows did not initialize the virtual device.");
}
struct Hid {
    HANDLE file=INVALID_HANDLE_VALUE;
    ~Hid(){if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);}
    void Open(USHORT vid,USHORT pid){
        GUID guid;HidD_GetHidGuid(&guid);const auto devices=SetupDiGetClassDevsW(&guid,nullptr,nullptr,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
        if(devices==INVALID_HANDLE_VALUE)throw std::runtime_error("Enumerate HID interfaces");
        for(DWORD index=0;file==INVALID_HANDLE_VALUE;++index){
            SP_DEVICE_INTERFACE_DATA item{};item.cbSize=sizeof(item);if(!SetupDiEnumDeviceInterfaces(devices,nullptr,&guid,index,&item))break;
            DWORD required=0;SetupDiGetDeviceInterfaceDetailW(devices,&item,nullptr,0,&required,nullptr);if(required<sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)||required>65536)continue;
            std::vector<std::uint8_t> storage(required);auto* detail=reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(storage.data());detail->cbSize=sizeof(*detail);
            if(!SetupDiGetDeviceInterfaceDetailW(devices,&item,detail,required,nullptr,nullptr))continue;
            auto candidate=CreateFileW(detail->DevicePath,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr);
            if(candidate==INVALID_HANDLE_VALUE)continue;HIDD_ATTRIBUTES attributes{};attributes.Size=sizeof(attributes);
            if(HidD_GetAttributes(candidate,&attributes)&&attributes.VendorID==vid&&attributes.ProductID==pid)file=candidate;else CloseHandle(candidate);
        }
        SetupDiDestroyDeviceInfoList(devices);Check(file!=INVALID_HANDLE_VALUE,"Open virtual DS4 HID handle");
    }
    Examples::Bytes Read(DWORD timeout){
        OVERLAPPED operation{};operation.hEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);Check(operation.hEvent!=nullptr,"Create read event");
        Examples::Bytes data(64);DWORD length=0;const BOOL completed=ReadFile(file,data.data(),static_cast<DWORD>(data.size()),&length,&operation);
        DWORD error=completed?0:GetLastError();
        if(error==ERROR_IO_PENDING){if(WaitForSingleObject(operation.hEvent,timeout)==WAIT_TIMEOUT)CancelIoEx(file,&operation);if(!GetOverlappedResult(file,&operation,&length,TRUE))error=GetLastError();else error=0;}
        CloseHandle(operation.hEvent);if(error==ERROR_OPERATION_ABORTED)return {};Check(error==0,"Read virtual HID report");data.resize(length);return data;
    }
};
void Basic(Examples::Protocol& model){
    Examples::Session session(model,[](const std::string& text){std::cout<<text<<'\n';});Ready(session);
    if(model.relative)session.Apply({{"x",17},{"y",-23},{"wheel",1}});else{session.Apply({{"key1",4},{"modifiers",2}});session.Apply({},true);}
    const auto deadline=Examples::Now()+2000;bool consumed=false;
    while(Examples::Now()<deadline){auto info=Vdevice::Get_Info();Examples::Require(info);for(const auto& device:info.devices)if(device.inputs_completed>=2)consumed=true;if(consumed)break;Sleep(10);}
    Check(consumed,"Windows did not consume keyboard/mouse USB reports");session.Close();std::cout<<"PASS USB stack consumed "<<model.title<<" reports (desktop typing not asserted).\n";
}
void Xinput(Examples::Protocol& model,bool gip){
    std::atomic<unsigned> motor{0};
    Examples::Session session(model,[&](const std::string& text){std::cout<<text<<'\n';if(text.find("rumble:")!=std::string::npos||text.find("motor command:")!=std::string::npos)++motor;});Ready(session);
    session.Apply({{"buttons",4096},{"lx",12345},{"ly",-23456},{"rx",-12345},{"ry",23456},{"lt",gip?1023:255},{"rt",gip?1023:255}});
    DWORD slot=XUSER_MAX_COUNT;const auto deadline=Examples::Now()+5000;
    while(Examples::Now()<deadline&&slot==XUSER_MAX_COUNT){for(DWORD i=0;i<XUSER_MAX_COUNT;++i){XINPUT_STATE state{};if(XInputGetState(i,&state)==ERROR_SUCCESS&&state.Gamepad.wButtons==4096&&state.Gamepad.sThumbLX==12345&&state.Gamepad.sThumbLY==-23456&&state.Gamepad.sThumbRX==-12345&&state.Gamepad.sThumbRY==23456&&state.Gamepad.bLeftTrigger==255&&state.Gamepad.bRightTrigger==255){slot=i;break;}}Sleep(10);}
    if(slot==XUSER_MAX_COUNT)for(DWORD i=0;i<XUSER_MAX_COUNT;++i){XINPUT_STATE state{};const auto status=XInputGetState(i,&state);std::cout<<"XInput slot="<<i<<" status="<<status<<" buttons="<<state.Gamepad.wButtons<<" lx="<<state.Gamepad.sThumbLX<<" ly="<<state.Gamepad.sThumbLY<<" rx="<<state.Gamepad.sThumbRX<<" ry="<<state.Gamepad.sThumbRY<<" lt="<<unsigned(state.Gamepad.bLeftTrigger)<<" rt="<<unsigned(state.Gamepad.bRightTrigger)<<'\n';}
    if(slot==XUSER_MAX_COUNT)std::cout<<session.Info()<<'\n';
    Check(slot<XUSER_MAX_COUNT,"XInput did not receive the exact virtual state");
    XINPUT_VIBRATION vibration{};vibration.wLeftMotorSpeed=0x8000;vibration.wRightMotorSpeed=0x4000;Check(XInputSetState(slot,&vibration)==ERROR_SUCCESS,"XInputSetState failed");
    const auto feedbackDeadline=Examples::Now()+2000;while(!motor&&Examples::Now()<feedbackDeadline)Sleep(10);Check(motor!=0,"Host rumble did not reach the example");
    for(int i=0;i<2000;++i)session.Apply({{"lx",i-1000},{"buttons",(i&1)?4096:0}});
    session.Apply({},true);bool neutral=false;const auto releaseDeadline=Examples::Now()+3000;
    while(Examples::Now()<releaseDeadline){XINPUT_STATE state{};if(XInputGetState(slot,&state)==ERROR_SUCCESS&&state.Gamepad.wButtons==0&&state.Gamepad.sThumbLX==0&&state.Gamepad.bLeftTrigger==0){neutral=true;break;}Sleep(10);}
    if(!neutral){XINPUT_STATE state{};const auto error=XInputGetState(slot,&state);std::cout<<"Neutral missing: status="<<error<<" slot="<<slot<<" buttons="<<state.Gamepad.wButtons<<" lx="<<state.Gamepad.sThumbLX<<" ly="<<state.Gamepad.sThumbLY<<" lt="<<unsigned(state.Gamepad.bLeftTrigger)<<" rt="<<unsigned(state.Gamepad.bRightTrigger)<<"\n"<<session.Info()<<'\n';}
    Check(neutral,"XInput neutral state not received");session.Close();std::cout<<"PASS "<<model.title<<": real XInput state, rumble, 2000 updates and release.\n";
}
void Ds4(){
    DualShock4Example::DualShock4 model;std::atomic<unsigned> feedback{0};
    Examples::Session session(model,[&](const std::string& text){std::cout<<text<<'\n';if(text.find("DS4 output:")!=std::string::npos)++feedback;});Ready(session);Hid hid;hid.Open(0x054C,0x05C4);
    std::array<std::uint8_t,64> feature{};feature[0]=0x12;Check(HidD_GetFeature(hid.file,feature.data(),static_cast<ULONG>(feature.size()))&&feature[0]==0x12,"DS4 MAC feature reply");
    session.Apply({{"buttons",2},{"hat",3},{"lx",17},{"ly",231},{"rt",190},{"gyro_x",-1234},{"touch1",1},{"touch1_x",1567},{"touch1_y",789}});
    bool matched=false;const auto deadline=Examples::Now()+3000;
    while(Examples::Now()<deadline){const auto bytes=hid.Read(100);if(bytes.size()==64&&bytes[1]==17&&bytes[2]==231&&bytes[5]==0x23&&bytes[9]==190&&bytes[13]==0x2E&&bytes[14]==0xFB&&(bytes[35]&128)==0&&(bytes[36]|((bytes[37]&15)<<8))==1567&&((bytes[37]>>4)|(bytes[38]<<4))==789){matched=true;break;}}
    Check(matched,"Windows HID did not receive exact DS4 state/touch/motion bytes");
    const auto previous=feedback.load();std::array<std::uint8_t,32> output{};output[0]=5;output[1]=3;output[4]=77;output[5]=123;output[6]=20;output[7]=40;output[8]=60;
    Check(HidD_SetOutputReport(hid.file,output.data(),static_cast<ULONG>(output.size()))!=FALSE,"DS4 host output failed");
    const auto until=Examples::Now()+2000;while(feedback==previous&&Examples::Now()<until)Sleep(10);Check(feedback>previous,"DS4 feedback not returned to example");
    for(int i=0;i<2000;++i)session.Apply({{"lx",i&255}});session.Apply({},true);CloseHandle(hid.file);hid.file=INVALID_HANDLE_VALUE;session.Close();std::cout<<"PASS DS4: real HID state/touch/motion, feature and host output; 2000 updates.\n";
}
}
int main(int argc,char** argv){
    try{Guard(argc,argv);KeyboardExample::Keyboard keyboard;MouseExample::Mouse mouse;Basic(keyboard);Basic(mouse);Xbox360Example::Xbox360 xbox;Xinput(xbox,false);Ds4();XboxOneGipExample::XboxOneGip gip;Xinput(gip,true);std::cout<<"PASS all five virtual-device examples.\n";return 0;}
    catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}
}
