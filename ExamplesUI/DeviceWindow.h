#pragma once
#include "../ExamplesConsole/ExampleRuntime.h"
#include <commctrl.h>
#include <shellapi.h>
#include <memory>
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace Examples {
inline std::wstring Wide(const std::string& text){
    if(text.empty())return {};const int length=MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),nullptr,0);
    std::wstring result(static_cast<std::size_t>(length),L' ');MultiByteToWideChar(CP_UTF8,0,text.data(),static_cast<int>(text.size()),&result[0],length);return result;
}
class DevicePanel {
    static constexpr UINT LogMessage=WM_APP+17;
    enum {Create=100,Disconnect,Send,Neutral,DelayedTap,StateTimer=1,TapTimer=2,ReleaseTimer=3};
    Protocol& model;
    HWND window=nullptr,log=nullptr,status=nullptr;
    HFONT font=nullptr,titleFont=nullptr;
    std::vector<HWND> edits,checks;
    std::vector<Field> initialFields;
    std::vector<std::pair<std::string,int>> tapState;
    std::atomic<unsigned> pendingLogs{0};
    Session session;
    bool preview=false;
    HWND Control(const wchar_t* type,const std::wstring& text,DWORD style,int x,int y,int width,int height,int id=0){
        auto control=CreateWindowExW(type==std::wstring(L"EDIT")?WS_EX_CLIENTEDGE:0,type,text.c_str(),WS_CHILD|WS_VISIBLE|style,x,y,width,height,window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),GetModuleHandleW(nullptr),nullptr);
        if(!control)throw std::runtime_error("Could not create a window control.");SendMessageW(control,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);return control;
    }
    void Append(const std::wstring& text){
        // Bound the visible log. Host data never grows the edit buffer forever.
        if(GetWindowTextLengthW(log)>48000)SetWindowTextW(log,L"");
        SendMessageW(log,EM_SETSEL,static_cast<WPARAM>(-1),static_cast<LPARAM>(-1));
        const auto line=text+L"\r\n";SendMessageW(log,EM_REPLACESEL,FALSE,reinterpret_cast<LPARAM>(line.c_str()));
    }
    void QueueLog(const std::string& text){
        if(pendingLogs.fetch_add(1)>=64){--pendingLogs;return;}
        auto message=std::make_unique<std::wstring>(Wide(text));
        if(window&&PostMessageW(window,LogMessage,0,reinterpret_cast<LPARAM>(message.get())))message.release();else --pendingLogs;
    }
    std::vector<std::pair<std::string,int>> Changes(){
        std::vector<std::pair<std::string,int>> changes;
        for(std::size_t i=0;i<edits.size();++i){wchar_t text[80]{};GetWindowTextW(edits[i],text,80);std::string narrow;for(const auto ch:std::wstring(text)){if(ch>127)throw std::invalid_argument("Use decimal or 0x-prefixed numeric fields.");narrow.push_back(static_cast<char>(ch));}changes.emplace_back(initialFields[i].name,Number(narrow));}
        return changes;
    }
    void RefreshFields(){
        const auto values=session.Fields();
        for(std::size_t i=0;i<edits.size();++i)SetWindowTextW(edits[i],std::to_wstring(values[i].value).c_str());
        int value=0;for(const auto& field:values)if(model.button_field==field.name)value=field.value;
        for(std::size_t i=0;i<checks.size();++i)SendMessageW(checks[i],BM_SETCHECK,(static_cast<std::uint32_t>(value)&model.buttons[i].mask)?BST_CHECKED:BST_UNCHECKED,0);
    }
    void UpdateStatus(){
        const auto text=preview?L"OFFLINE PREVIEW — no device created":session.IsReady()?L"READY — reports can be sent":session.IsOpen()?L"CREATED — waiting for Windows / protocol initialization":L"DISCONNECTED — create a virtual device to begin";
        SetWindowTextW(status,text);
        EnableWindow(GetDlgItem(window,Create),!preview&&!session.IsOpen());EnableWindow(GetDlgItem(window,Disconnect),!preview&&session.IsOpen());
        EnableWindow(GetDlgItem(window,Send),!preview&&session.IsReady());EnableWindow(GetDlgItem(window,Neutral),!preview&&session.IsReady());EnableWindow(GetDlgItem(window,DelayedTap),!preview&&session.IsReady());
    }
    void Build(){
        font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        titleFont=CreateFontW(-26,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        auto title=Control(L"STATIC",Wide("Vdevice  /  "+model.title),0,24,20,1000,38);SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(titleFont),TRUE);
        Control(L"STATIC",Wide(model.description),0,25,64,1000,25);
        status=Control(L"STATIC",L"",0,25,98,1000,25);
        Control(L"BUTTON",L"Create device",BS_PUSHBUTTON|WS_TABSTOP,24,138,150,36,Create);
        Control(L"BUTTON",L"Disconnect",BS_PUSHBUTTON|WS_TABSTOP,184,138,135,36,Disconnect);
        Control(L"BUTTON",L"Send state",BS_DEFPUSHBUTTON|WS_TABSTOP,500,138,150,36,Send);
        Control(L"BUTTON",L"Release / neutral",BS_PUSHBUTTON|WS_TABSTOP,660,138,160,36,Neutral);
        Control(L"BUTTON",L"Tap state in 3 s",BS_PUSHBUTTON|WS_TABSTOP,830,138,180,36,DelayedTap);
        initialFields=model.fields;
        for(std::size_t i=0;i<initialFields.size();++i){
            const auto& field=initialFields[i];const int x=24+int(i%4)*250,y=194+int(i/4)*60;
            Control(L"STATIC",Wide(std::string(field.name)+"   "+std::to_string(field.minimum)+" .. "+std::to_string(field.maximum)),0,x,y,239,22);
            edits.push_back(Control(L"EDIT",std::to_wstring(field.value),ES_AUTOHSCROLL|WS_TABSTOP,x,y+23,225,27));
        }
        const int after=194+int((initialFields.size()+3)/4)*60+12;
        for(std::size_t i=0;i<model.buttons.size();++i)checks.push_back(Control(L"BUTTON",Wide(model.buttons[i].name),BS_AUTOCHECKBOX|WS_TABSTOP,24+int(i%8)*124,after+int(i/8)*29,122,26,200+static_cast<int>(i)));
        const int explanation=after+int((model.buttons.size()+7)/8)*29+10;
        Control(L"STATIC",Wide(model.legend),0,24,explanation,984,60);
        Control(L"STATIC",L"Host feedback and status  ·  Create starts the shared two-hour demo allowance",0,24,explanation+68,990,25);
        log=Control(L"EDIT",L"",ES_MULTILINE|ES_READONLY|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP,24,explanation+96,984,155);
        const int height=explanation+285;RECT bounds{0,0,1036,height};AdjustWindowRectEx(&bounds,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE,0);
        SetWindowPos(window,nullptr,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,SWP_NOMOVE|SWP_NOZORDER);
        RefreshFields();UpdateStatus();SetTimer(window,StateTimer,250,nullptr);
        Append(preview?L"Preview mode: creation and sending are disabled.":L"Install Vdevice first, then run this example as administrator. Choose values and click Send state. Tap state waits 3 seconds so you can focus another application, sends the selected state, then releases it after 50 ms.");
    }
    LRESULT Message(UINT message,WPARAM wp,LPARAM lp){
        if(message==WM_CREATE){Build();return 0;}
        if(message==LogMessage){std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lp));--pendingLogs;Append(*text);return 0;}
        if(message==WM_TIMER){
            if(wp==StateTimer)UpdateStatus();
            else if(wp==TapTimer){KillTimer(window,TapTimer);session.Apply(tapState);SetTimer(window,ReleaseTimer,50,nullptr);}
            else if(wp==ReleaseTimer){KillTimer(window,ReleaseTimer);session.Apply({},true);RefreshFields();}
            return 0;
        }
        if(message==WM_COMMAND && HIWORD(wp)==BN_CLICKED){
            const int command=LOWORD(wp);
            if(command==Create){
                HANDLE token=nullptr;TOKEN_ELEVATION elevation{};DWORD returned=0;
                if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))throw std::runtime_error("Could not check administrator rights.");
                const BOOL queried=GetTokenInformation(token,TokenElevation,&elevation,sizeof(elevation),&returned);CloseHandle(token);
                if(!queried)throw std::runtime_error("Could not check administrator rights.");
                if(!elevation.TokenIsElevated){
                    wchar_t path[32768]{};if(!GetModuleFileNameW(nullptr,path,32768))throw std::runtime_error("Could not locate the example executable.");
                    const auto started=reinterpret_cast<INT_PTR>(ShellExecuteW(window,L"runas",path,L"--create",nullptr,SW_SHOWNORMAL));
                    if(started<=32)throw std::runtime_error("Administrator confirmation was cancelled or the elevated example could not start.");
                    PostMessageW(window,WM_CLOSE,0,0);return 0;
                }
                session.Open();
            }
            else if(command==Disconnect){KillTimer(window,TapTimer);KillTimer(window,ReleaseTimer);session.Close();RefreshFields();}
            else if(command==Send){session.Apply(Changes());RefreshFields();Append(L"State sent.");}
            else if(command==Neutral){session.Apply({},true);RefreshFields();}
            else if(command==DelayedTap){tapState=Changes();SetTimer(window,TapTimer,3000,nullptr);Append(L"Tap scheduled. Focus the target application within three seconds.");}
            else if(command>=200 && command<200+static_cast<int>(checks.size())){
                const auto values=Changes();int value=0;std::size_t index=0;
                for(std::size_t i=0;i<values.size();++i)if(values[i].first==model.button_field){value=values[i].second;index=i;}
                const auto bit=model.buttons[static_cast<std::size_t>(command-200)].mask;
                if(SendMessageW(checks[static_cast<std::size_t>(command-200)],BM_GETCHECK,0,0)==BST_CHECKED)value|=static_cast<int>(bit);else value&=~static_cast<int>(bit);
                SetWindowTextW(edits[index],std::to_wstring(value).c_str());
            }
            UpdateStatus();return 0;
        }
        if(message==WM_CLOSE){KillTimer(window,StateTimer);KillTimer(window,TapTimer);KillTimer(window,ReleaseTimer);session.Close();MSG pending{};while(PeekMessageW(&pending,window,LogMessage,LogMessage,PM_REMOVE)){delete reinterpret_cast<std::wstring*>(pending.lParam);--pendingLogs;}DestroyWindow(window);return 0;}
        if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
        return DefWindowProcW(window,message,wp,lp);
    }
    static LRESULT CALLBACK Procedure(HWND handle,UINT message,WPARAM wp,LPARAM lp)noexcept{
        auto* panel=reinterpret_cast<DevicePanel*>(GetWindowLongPtrW(handle,GWLP_USERDATA));
        if(message==WM_NCCREATE){panel=static_cast<DevicePanel*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);panel->window=handle;SetWindowLongPtrW(handle,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(panel));}
        try{if(panel)return panel->Message(message,wp,lp);}catch(const std::exception& error){
            if(panel&&panel->log)panel->Append(Wide(std::string("Error: ")+error.what()));
            if(message==WM_CREATE)return -1;return 0;
        }catch(...){return 0;}
        return DefWindowProcW(handle,message,wp,lp);
    }
public:
    DevicePanel(Protocol& protocol,bool offline):model(protocol),session(model,[this](const std::string& text){QueueLog(text);}),preview(offline){}
    ~DevicePanel(){session.Close();if(font)DeleteObject(font);if(titleFont)DeleteObject(titleFont);}
    int Run(HINSTANCE instance,int show,bool create=false){
        WNDCLASSEXW cls{};cls.cbSize=sizeof(cls);cls.lpfnWndProc=Procedure;cls.hInstance=instance;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);cls.lpszClassName=L"VdeviceExampleWindow";
        if(!RegisterClassExW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("Could not register example window.");
        const auto title=Wide("Vdevice · "+model.title);
        window=CreateWindowExW(0,cls.lpszClassName,title.c_str(),WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,1050,850,nullptr,nullptr,instance,this);
        if(!window)throw std::runtime_error("Could not create example window.");ShowWindow(window,show);UpdateWindow(window);
        if(create)PostMessageW(window,WM_COMMAND,Create,0);
        MSG message{};BOOL result;while((result=GetMessageW(&message,nullptr,0,0))>0){if(!IsDialogMessageW(window,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}
        return result<0?1:static_cast<int>(message.wParam);
    }
};
inline int Window(Protocol& model,HINSTANCE instance,int show){
    try {
        int count=0;auto* parts=CommandLineToArgvW(GetCommandLineW(),&count);
        if(!parts)throw std::runtime_error("Could not parse the command line.");
        const std::wstring arguments=count==2?parts[1]:L"";LocalFree(parts);
        if(count>2)throw std::invalid_argument("Use one optional argument: --preview.");
        const bool preview=arguments==L"--preview";
        if(!arguments.empty()&&!preview&&arguments!=L"--create")throw std::invalid_argument("Use no arguments, or --preview for an offline window without a device.");
        if(!preview)CheckInstallation();
        INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES};InitCommonControlsEx(&controls);
        DevicePanel panel(model,preview);return panel.Run(instance,show,arguments==L"--create");
    }catch(const std::exception& error){MessageBoxW(nullptr,Wide(error.what()).c_str(),L"Vdevice example",MB_OK|MB_ICONERROR);return 1;}
}
}
