#pragma once
#include "ExampleRuntime.h"
#ifdef VDEVICE_EXAMPLE_UI
#include "../ExamplesUI/DeviceWindow.h"
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int show){
    VDEVICE_EXAMPLE_MODEL model;return Examples::Window(model,instance,show);
}
#else
int main(int argc,char** argv){VDEVICE_EXAMPLE_MODEL model;return Examples::Console(model,argc,argv);}
#endif
