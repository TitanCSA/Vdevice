/* SPDX-License-Identifier: MIT */
#ifndef VDEVICE_CSDK_H
#define VDEVICE_CSDK_H
#include <stdint.h>
#if !defined(_WIN32) || !defined(_WIN64)
#error This SDK requires Windows x64.
#endif
#ifdef __cplusplus
#define VDEVICE_CSDK_NOEXCEPT noexcept
extern "C" {
#else
#define VDEVICE_CSDK_NOEXCEPT
#endif
#if defined(VDEVICE_CSDK_BUILD)
#define VDEVICE_CSDK_API __declspec(dllexport)
#else
#define VDEVICE_CSDK_API __declspec(dllimport)
#endif
/* open is inert. One active session per loaded product DLL.
   call accepts length-delimited UTF-8 JSON and returns an allocated UTF-8
   response, or NULL on allocation failure. Always free with this same DLL.
   Session/operation handles are scoped to the client that returned them. */
VDEVICE_CSDK_API uint32_t vdevice_json_open(uint64_t *session) VDEVICE_CSDK_NOEXCEPT;
VDEVICE_CSDK_API uint32_t vdevice_json_close(uint64_t session) VDEVICE_CSDK_NOEXCEPT;
VDEVICE_CSDK_API char *vdevice_json_call(uint64_t session,const char *request,uint32_t length) VDEVICE_CSDK_NOEXCEPT;
VDEVICE_CSDK_API void vdevice_json_free(char *response) VDEVICE_CSDK_NOEXCEPT;
#ifdef __cplusplus
}
#endif
#undef VDEVICE_CSDK_NOEXCEPT
#endif
