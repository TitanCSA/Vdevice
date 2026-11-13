#include "VdeviceJsonSDK.h"
#include <string.h>
int main(void){uint64_t session=0;if(vdevice_json_open(&session))return 1;char *reply=vdevice_json_call(session,"{",1);if(!reply){vdevice_json_close(session);return 2;}int failed=strstr(reply,"\"ok\":false")==0;vdevice_json_free(reply);if(vdevice_json_close(session))return 3;return failed;}
