#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <initguid.h>
#include <devpkey.h>
#include <setupapi.h>
#include <newdev.h>
#include <cfgmgr32.h>
#include <wintrust.h>
#include <softpub.h>
#include <mscat.h>
#include <psapi.h>
#include <algorithm>
#include <cwctype>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <io.h>
#include <fcntl.h>
#include <set>
#include <string>
#include <vector>
#include "Protocol.h"

namespace {
struct DeviceSet {
    HDEVINFO value = INVALID_HANDLE_VALUE;
    ~DeviceSet() { if (value != INVALID_HANDLE_VALUE) SetupDiDestroyDeviceInfoList(value); }
};
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct HashProvider {
    HCRYPTPROV provider = 0; HCRYPTHASH hash = 0;
    ~HashProvider() { if (hash) CryptDestroyHash(hash); if (provider) CryptReleaseContext(provider, 0); }
};
bool FileSha256(const std::wstring& path, std::wstring& result, DWORD& error) {
    error = 0; result.clear();
    Handle file; file.value = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file.value == INVALID_HANDLE_VALUE) { error = GetLastError(); return false; }
    LARGE_INTEGER size = {};
    if (!GetFileSizeEx(file.value, &size)) { error = GetLastError(); return false; }
    if (size.QuadPart <= 0 || size.QuadPart > 64 * 1024 * 1024) { error = ERROR_FILE_TOO_LARGE; return false; }
    HashProvider hash;
    if (!CryptAcquireContextW(&hash.provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT | CRYPT_SILENT)
        || !CryptCreateHash(hash.provider, CALG_SHA_256, 0, 0, &hash.hash)) { error = GetLastError(); return false; }
    BYTE block[65536]; DWORD count = 0;
    for (;;) {
        if (!ReadFile(file.value, block, sizeof(block), &count, nullptr)) { error = GetLastError(); return false; }
        if (!count) break;
        if (!CryptHashData(hash.hash, block, count, 0)) { error = GetLastError(); return false; }
    }
    BYTE digest[32] = {}; DWORD digestBytes = sizeof(digest);
    if (!CryptGetHashParam(hash.hash, HP_HASHVAL, digest, &digestBytes, 0) || digestBytes != sizeof(digest)) { error = GetLastError(); return false; }
    const wchar_t* hex = L"0123456789ABCDEF"; result.resize(64);
    for (DWORD i = 0; i < digestBytes; ++i) { result[i * 2] = hex[digest[i] >> 4]; result[i * 2 + 1] = hex[digest[i] & 15]; }
    return true;
}
struct DebugPrivilege {
    HANDLE token = nullptr; TOKEN_PRIVILEGES previous = {}; bool restore = false;
    DebugPrivilege() {
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) return;
        TOKEN_PRIVILEGES desired = {}; desired.PrivilegeCount = 1;
        if (!LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &desired.Privileges[0].Luid)) return;
        desired.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED; DWORD bytes = 0;
        SetLastError(ERROR_SUCCESS);
        restore = AdjustTokenPrivileges(token, FALSE, &desired, sizeof(previous), &previous, &bytes) && GetLastError() != ERROR_NOT_ALL_ASSIGNED;
    }
    ~DebugPrivilege() { if (restore) AdjustTokenPrivileges(token, FALSE, &previous, 0, nullptr, nullptr); if (token) CloseHandle(token); }
};
std::wstring ErrorText(DWORD error) {
    wchar_t* buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring result = buffer ? buffer : L"No system description available.";
    if (buffer) LocalFree(buffer);
    while (!result.empty() && iswspace(result.back())) result.pop_back();
    return result;
}
std::wstring NtStatusText(DWORD status) {
    wchar_t* buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_HMODULE | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        GetModuleHandleW(L"ntdll.dll"), status, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    std::wstring result = buffer ? buffer : L"No NTSTATUS description available.";
    if (buffer) LocalFree(buffer);
    while (!result.empty() && iswspace(result.back())) result.pop_back();
    return result;
}
int Failure(const wchar_t* step, DWORD error) {
    std::wcerr << L"ERROR: " << step << L" [" << error << L" / 0x" << std::hex << error << std::dec
        << L"] " << ErrorText(error) << L"\n";
    return error && error < 65536 ? static_cast<int>(error) : 1;
}
std::wstring Lower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return text;
}
bool Matches(HDEVINFO set, SP_DEVINFO_DATA& device) {
    DWORD type = 0, required = 0;
    SetupDiGetDeviceRegistryPropertyW(set, &device, SPDRP_HARDWAREID, &type, nullptr, 0, &required);
    if (!required || required > 65536 || type != REG_MULTI_SZ) return false;
    std::vector<BYTE> raw(required + sizeof(wchar_t) * 2, 0);
    if (!SetupDiGetDeviceRegistryPropertyW(set, &device, SPDRP_HARDWAREID, &type, raw.data(), required, nullptr)) return false;
    const wchar_t* id = reinterpret_cast<const wchar_t*>(raw.data());
    bool found = false;
    for (; *id; id += wcslen(id) + 1) if (_wcsicmp(id, VD_HARDWARE_ID) == 0) found = true;
    wchar_t instance[MAX_DEVICE_ID_LEN] = {};
    return found && SetupDiGetDeviceInstanceIdW(set, &device, instance, MAX_DEVICE_ID_LEN, nullptr)
        && Lower(instance).find(L"root\\") == 0;
}
std::wstring InfName(HDEVINFO set, SP_DEVINFO_DATA& device) {
    HKEY key = SetupDiOpenDevRegKey(set, &device, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_QUERY_VALUE);
    if (key == INVALID_HANDLE_VALUE) return {};
    wchar_t text[MAX_PATH] = {}; DWORD size = sizeof(text), type = 0;
    LONG status = RegQueryValueExW(key, L"InfPath", nullptr, &type, reinterpret_cast<BYTE*>(text), &size);
    RegCloseKey(key);
    return status == ERROR_SUCCESS && type == REG_SZ ? text : L"";
}
bool IsOwnInf(const std::wstring& path) {
    UINT errorLine = 0;
    HINF inf = SetupOpenInfFileW(path.c_str(), nullptr, INF_STYLE_WIN4, &errorLine);
    if (inf == INVALID_HANDLE_VALUE) return false;
    INFCONTEXT context = {}; wchar_t provider[256] = {}, catalog[256] = {};
    bool own = SetupFindFirstLineW(inf, L"Version", L"Provider", &context)
        && SetupGetStringFieldW(&context, 1, provider, 256, nullptr)
        && (_wcsicmp(provider, L"TitanCSA") == 0 || _wcsicmp(provider, L"Vdevice") == 0)
        && SetupFindFirstLineW(inf, L"Version", L"CatalogFile", &context)
        && SetupGetStringFieldW(&context, 1, catalog, 256, nullptr) && _wcsicmp(catalog, L"Vdevice.cat") == 0;
    bool hardware = false, service = false;
    for (UINT sectionIndex = 0; own && sectionIndex < 1024; ++sectionIndex) {
        wchar_t section[256] = {};
        if (!SetupEnumInfSectionsW(inf, sectionIndex, section, 256, nullptr)) break;
        for (UINT lineIndex = 0; lineIndex < 32768; ++lineIndex) {
            if (!SetupGetLineByIndexW(inf, section, lineIndex, &context)) break;
            wchar_t first[1024] = {}, second[1024] = {}, key[256] = {};
            SetupGetStringFieldW(&context, 0, key, 256, nullptr);
            SetupGetStringFieldW(&context, 1, first, 1024, nullptr);
            SetupGetStringFieldW(&context, 2, second, 1024, nullptr);
            if (_wcsicmp(second, VD_HARDWARE_ID) == 0) hardware = true;
            if (_wcsicmp(key, L"AddService") == 0 && _wcsicmp(first, VD_SERVICE) == 0) service = true;
        }
    }
    own = own && hardware && service;
    SetupCloseInfFile(inf);
    return own;
}
LONG CheckCatalog(const std::wstring& path) {
    WINTRUST_FILE_INFO file = {}; file.cbStruct = sizeof(file); file.pcwszFilePath = path.c_str();
    WINTRUST_DATA data = {}; data.cbStruct = sizeof(data); data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN; data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &file; data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG result = WinVerifyTrust(nullptr, &action, &data);
    data.dwStateAction = WTD_STATEACTION_CLOSE; WinVerifyTrust(nullptr, &action, &data);
    return result;
}
std::wstring ImagePath(std::wstring path) {
    if (path.size() >= 2 && path.front() == L'"' && path.back() == L'"') path = path.substr(1, path.size() - 2);
    if (path.find(L"\\??\\") == 0) path = path.substr(4);
    const std::wstring systemRoot = L"\\SystemRoot\\";
    wchar_t windows[MAX_PATH] = {};
    if (GetWindowsDirectoryW(windows, MAX_PATH)) {
        if (_wcsnicmp(path.c_str(), systemRoot.c_str(), systemRoot.size()) == 0) path = std::wstring(windows) + L"\\" + path.substr(systemRoot.size());
        else if (_wcsnicmp(path.c_str(), L"system32\\", 9) == 0) path = std::wstring(windows) + L"\\" + path;
    }
    return path;
}
LONG CheckImageTrust(const std::wstring& path) {
    LONG embedded = CheckCatalog(path);
    if (embedded == ERROR_SUCCESS) return embedded;
    const auto slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return embedded;
    std::wstring catalogPath = path.substr(0, slash + 1) + L"Vdevice.cat";
    if (GetFileAttributesW(catalogPath.c_str()) == INVALID_FILE_ATTRIBUTES) return embedded;
    Handle file; file.value = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file.value == INVALID_HANDLE_VALUE) return static_cast<LONG>(GetLastError());
    HCATADMIN admin = nullptr;
    if (!CryptCATAdminAcquireContext2(&admin, nullptr, L"SHA256", nullptr, 0)) return static_cast<LONG>(GetLastError());
    BYTE hash[64] = {}; DWORD hashLength = sizeof(hash);
    if (!CryptCATAdminCalcHashFromFileHandle2(admin, file.value, &hashLength, hash, 0)) {
        DWORD error = GetLastError(); CryptCATAdminReleaseContext(admin, 0); return static_cast<LONG>(error);
    }
    wchar_t tag[129] = {}; const wchar_t* hex = L"0123456789ABCDEF";
    for (DWORD index = 0; index < hashLength; ++index) { tag[index * 2] = hex[hash[index] >> 4]; tag[index * 2 + 1] = hex[hash[index] & 15]; }
    WINTRUST_CATALOG_INFO member = {}; member.cbStruct = sizeof(member); member.pcwszCatalogFilePath = catalogPath.c_str();
    member.pcwszMemberTag = tag; member.pcwszMemberFilePath = path.c_str(); member.hMemberFile = file.value;
    member.pbCalculatedFileHash = hash; member.cbCalculatedFileHash = hashLength; member.hCatAdmin = admin;
    WINTRUST_DATA data = {}; data.cbStruct = sizeof(data); data.dwUIChoice = WTD_UI_NONE; data.dwUnionChoice = WTD_CHOICE_CATALOG;
    data.pCatalog = &member; data.dwStateAction = WTD_STATEACTION_VERIFY;
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL | WTD_REVOCATION_CHECK_NONE;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2; LONG trust = WinVerifyTrust(nullptr, &action, &data);
    data.dwStateAction = WTD_STATEACTION_CLOSE; WinVerifyTrust(nullptr, &action, &data);
    CryptCATAdminReleaseContext(admin, 0); return trust;
}
struct ImageState { std::wstring path, sha256; DWORD error = 0; };
ImageState PrintImage(const wchar_t* label, const std::wstring& rawPath) {
    ImageState image; image.path = ImagePath(rawPath);
    std::wcout << label << image.path << L"\n";
    if (FileSha256(image.path, image.sha256, image.error)) std::wcout << L"Image SHA-256: " << image.sha256 << L"\n";
    else std::wcout << L"Image SHA-256: unavailable (" << image.error << L"): " << ErrorText(image.error) << L"\n";
    LONG trust = CheckImageTrust(image.path);
    std::wcout << L"Image signature/catalog trust: " << (trust == ERROR_SUCCESS ? L"trusted" : L"not verified")
        << L" [0x" << std::hex << std::setw(8) << std::setfill(L'0') << static_cast<DWORD>(trust) << std::dec << L"]\n";
    return image;
}
std::vector<ImageState> LoadedImages() {
    DebugPrivilege privilege;
    std::vector<ImageState> images;
    std::vector<LPVOID> drivers(1024); DWORD bytes = 0;
    if (!EnumDeviceDrivers(drivers.data(), static_cast<DWORD>(drivers.size() * sizeof(LPVOID)), &bytes) || bytes > drivers.size() * sizeof(LPVOID)) {
        std::wcout << L"Loaded module path: unavailable (" << GetLastError() << L")\n"; return images;
    }
    bool found = false;
    for (DWORD index = 0; index < bytes / sizeof(LPVOID); ++index) {
        wchar_t name[MAX_PATH] = {}, path[32768] = {};
        if (GetDeviceDriverBaseNameW(drivers[index], name, MAX_PATH) && _wcsicmp(name, L"Vdevice.sys") == 0
            && GetDeviceDriverFileNameW(drivers[index], path, 32768)) { images.push_back(PrintImage(L"Loaded driver: ", path)); found = true; }
    }
    if (!found) std::wcout << L"Loaded driver: not found, or Windows did not permit module-path inspection.\n";
    return images;
}
DWORD CompareImages(const std::wstring& expected, const ImageState& configured, const std::vector<ImageState>& loaded) {
    if (configured.sha256.empty()) {
        if (configured.path.empty()) { std::wcerr << L"No Vdevice service image is configured. Install the driver package first.\n"; return ERROR_NOT_FOUND; }
        std::wcerr << L"The configured service image could not be read, so its payload identity was not verified.\n";
        return configured.error ? configured.error : ERROR_NOT_READY;
    }
    if (configured.sha256 != expected) {
        std::wcerr << L"The configured SYS does not match this installer's payload. Windows may have reused a package with the same INF/DriverVer. Increase DriverVer, regenerate and sign the catalog, then rebuild the installer. Active Driver Store files were not deleted or overwritten.\n";
        return ERROR_REVISION_MISMATCH;
    }
    if (loaded.empty()) {
        std::wcerr << L"The configured SYS matches the payload, but its loaded module could not be verified. Restart Windows and run elevated diagnostics before relying on this installation.\n";
        return ERROR_NOT_READY;
    }
    for (const auto& image : loaded) {
        if (image.sha256.empty()) return image.error ? image.error : ERROR_NOT_READY;
        if (image.sha256 != expected) {
            std::wcerr << L"The configured SYS matches the payload, but an older/different SYS remains loaded. Restart Windows to activate the selected package.\n";
            return ERROR_SUCCESS_REBOOT_REQUIRED;
        }
    }
    std::wcout << L"Configured and loaded-module SYS files match the bundled payload SHA-256.\n";
    return ERROR_SUCCESS;
}
DWORD TimedControl(HANDLE handle, DWORD code, void* output, DWORD outputBytes, DWORD& transferred, DWORD timeout = 5000) {
    Handle event; event.value = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (event.value == INVALID_HANDLE_VALUE || !event.value) { event.value = INVALID_HANDLE_VALUE; return GetLastError(); }
    OVERLAPPED request = {}; request.hEvent = event.value;
    transferred = 0;
    if (DeviceIoControl(handle, code, nullptr, 0, output, outputBytes, &transferred, &request)) return ERROR_SUCCESS;
    DWORD error = GetLastError(); if (error != ERROR_IO_PENDING) return error;
    DWORD wait = WaitForSingleObject(event.value, timeout);
    if (wait == WAIT_OBJECT_0) return GetOverlappedResult(handle, &request, &transferred, FALSE) ? ERROR_SUCCESS : GetLastError();
    error = wait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
    std::wcout << L"The driver request exceeded its " << timeout << L" ms deadline; cancelling and draining it safely...\n";
    CancelIoEx(handle, &request);
    // OVERLAPPED and its output buffer must remain alive until cancellation has
    // completed. An unresponsive kernel driver cannot safely be abandoned by
    // releasing this memory; the outer installer keeps reporting its wait.
    GetOverlappedResult(handle, &request, &transferred, TRUE);
    return error;
}
DWORD QuiesceDriver() {
    std::wcout << L"Preparing driver removal: stopping virtual devices and waking pending application requests...\n";
    Handle api; api.value = CreateFileW(VD_PATH, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (api.value == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND || error == ERROR_DEVICE_NOT_CONNECTED || error == ERROR_DEV_NOT_EXIST) {
            std::wcout << L"No active driver control endpoint; continuing removal of Windows device records.\n"; return ERROR_SUCCESS;
        }
        return error;
    }
    DWORD transferred = 0;
    DWORD error = TimedControl(api.value, VD_QUIESCE, nullptr, 0, transferred);
    if (error == ERROR_INVALID_FUNCTION || error == ERROR_NOT_SUPPORTED) {
        std::wcerr << L"This loaded development driver does not support safe uninstall preparation. Close applications using Vdevice, then update to the current driver or restart Windows before retrying removal.\n";
        return ERROR_BUSY;
    }
    if (error != ERROR_SUCCESS) {
        std::wcerr << L"Driver removal preparation did not complete. The controller may already be paused; close Vdevice applications and retry uninstall or restart Windows.\n";
        return error;
    }
    std::wcout << L"Virtual devices stopped and pending application requests completed. Closing the setup control handle before Windows PnP removal.\n";
    return ERROR_SUCCESS;
}
int Status(const std::wstring& expectedImage = L"") {
    DeviceSet set; set.value = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES);
    if (set.value == INVALID_HANDLE_VALUE) return Failure(L"Enumerate Windows devices", GetLastError());
    unsigned matches = 0; bool pnpProblem = false;
    for (DWORD index = 0;; ++index) {
        SP_DEVINFO_DATA device = {}; device.cbSize = sizeof(device);
        if (!SetupDiEnumDeviceInfo(set.value, index, &device)) {
            if (GetLastError() != ERROR_NO_MORE_ITEMS) return Failure(L"Read device inventory", GetLastError());
            break;
        }
        if (!Matches(set.value, device)) continue;
        ++matches; wchar_t instance[MAX_DEVICE_ID_LEN] = {}; DWORD state = 0, problem = 0;
        SetupDiGetDeviceInstanceIdW(set.value, &device, instance, MAX_DEVICE_ID_LEN, nullptr);
        CONFIGRET cr = CM_Get_DevNode_Status(&state, &problem, device.DevInst, 0);
        pnpProblem = pnpProblem || cr != CR_SUCCESS || problem != 0;
        std::wcout << L"Root device: " << instance << L"\nDriver INF: " << InfName(set.value, device)
            << L"\nPnP status: " << (cr == CR_SUCCESS ? L"available" : L"not present")
            << L"; problem code: " << problem << L"\n";
        DEVPROPTYPE propertyType = 0; DWORD problemStatus = 0; ULONG propertyBytes = sizeof(problemStatus);
        CONFIGRET propertyResult = CM_Get_DevNode_PropertyW(device.DevInst, &DEVPKEY_Device_ProblemStatus,
            &propertyType, reinterpret_cast<PBYTE>(&problemStatus), &propertyBytes, 0);
        if (propertyResult == CR_SUCCESS && propertyType == DEVPROP_TYPE_NTSTATUS && propertyBytes == sizeof(problemStatus))
            std::wcout << L"PnP problem status: 0x" << std::hex << std::setw(8) << std::setfill(L'0') << problemStatus << std::dec
                << L" — " << NtStatusText(problemStatus) << L"\n";
        else std::wcout << L"PnP problem status: unavailable (Configuration Manager " << propertyResult << L")\n";
    }
    std::wcout << L"Root devices: " << matches << L"\n";
    ImageState configured;
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm) {
        SC_HANDLE service = OpenServiceW(scm, VD_SERVICE, SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG);
        if (service) {
            SERVICE_STATUS_PROCESS state = {}; DWORD bytes = 0;
            if (QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<BYTE*>(&state), sizeof(state), &bytes))
                std::wcout << L"Service: " << (state.dwCurrentState == SERVICE_RUNNING ? L"running" : L"not running") << L"\n";
            DWORD configBytes = 0;
            QueryServiceConfigW(service, nullptr, 0, &configBytes);
            if (configBytes && configBytes <= 65536) {
                std::vector<BYTE> config(configBytes, 0);
                auto info = reinterpret_cast<QUERY_SERVICE_CONFIGW*>(config.data());
                if (QueryServiceConfigW(service, info, configBytes, &configBytes) && info->lpBinaryPathName) configured = PrintImage(L"Configured service image: ", info->lpBinaryPathName);
            }
            CloseServiceHandle(service);
        } else std::wcout << L"Service: absent (" << GetLastError() << L")\n";
        CloseServiceHandle(scm);
    }
    auto loaded = LoadedImages(); DWORD imageStatus = ERROR_SUCCESS;
    if (!expectedImage.empty()) {
        std::wstring expected; DWORD hashError = 0;
        if (!FileSha256(expectedImage, expected, hashError)) return Failure(L"Hash the bundled SYS for comparison", hashError);
        std::wcout << L"Bundled SYS SHA-256: " << expected << L"\n";
        imageStatus = CompareImages(expected, configured, loaded);
    }
    Handle api; api.value = CreateFileW(VD_PATH, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (api.value == INVALID_HANDLE_VALUE) {
        DWORD error = GetLastError(); std::wcout << L"Driver API: unavailable (" << error << L"): " << ErrorText(error) << L"\n";
        return imageStatus ? static_cast<int>(imageStatus) : matches ? 1 : 2;
    }
    VD_INFO info = {}; DWORD bytes = 0;
    DWORD queryError = TimedControl(api.value, VD_QUERY, &info, sizeof(info), bytes);
    if (queryError != ERROR_SUCCESS) return Failure(L"Query driver API", queryError);
    if (bytes != sizeof(info) || info.Version != VD_VERSION) return Failure(L"Driver ABI mismatch", ERROR_REVISION_MISMATCH);
    std::wcout << L"Driver API: responding; ABI " << info.Version << L"\nMaximum devices: " << info.MaxDevices
        << L"\nMaximum report bytes: " << info.MaxTransfer << L"\nUSB speeds: low=" << info.LowSpeed
        << L", full=" << info.FullSpeed << L", high=" << info.HighSpeed << L"\n";
    if (info.TrialExpired) std::wcout << L"Demo: expired; restart Windows to create devices again.\n";
    else if (info.TrialStarted100ns) std::wcout << L"Demo remaining: " << info.TrialRemaining100ns / 10000000u << L" seconds\n";
    else std::wcout << L"Demo: starts with the first successful virtual-device creation.\n";
    if (info.Reserved2) {
        std::wcout << L"Controller state: " << (info.Reserved2 == 1 ? L"quiescing" : L"removing")
            << L"; new virtual devices and reports are disabled. Close applications using Vdevice and finish uninstall; restart/reinstall to resume.\n";
        return ERROR_NOT_READY;
    }
    return pnpProblem ? 1 : static_cast<int>(imageStatus);
}
// /install deliberately means reinstall this exact bundled package, including
// when Windows ranks an already-installed development package above it.
DWORD StagePackage(const std::wstring& inf, BOOL& reboot,
    decltype(&DiInstallDriverW) install = &DiInstallDriverW,
    decltype(&SetupCopyOEMInfW) copy = &SetupCopyOEMInfW) {
    if (install(nullptr, inf.c_str(), DIIRFLAG_FORCE_INF, &reboot)) return ERROR_SUCCESS;
    DWORD error = GetLastError();
    if (error != ERROR_NO_MORE_ITEMS) return error;
    // Some Windows versions return NO_MORE_ITEMS after staging when no driver
    // candidate was selected. Stage explicitly, retaining SetupAPI's catalog
    // and package-member verification, then perform the exact-HWID force bind.
    std::wcout << L"Windows reported no selected driver candidate; staging the verified package for an explicit reinstall.\n";
    wchar_t published[MAX_PATH] = {};
    if (!copy(inf.c_str(), nullptr, SPOST_PATH, 0, published, MAX_PATH, nullptr, nullptr)) return GetLastError();
    std::wcout << L"Staged driver INF: " << published << L"\n";
    return ERROR_SUCCESS;
}
int Install(const std::wstring& path) {
    wchar_t absolute[32768] = {};
    if (!GetFullPathNameW(path.c_str(), 32768, absolute, nullptr)) return Failure(L"Resolve INF path", GetLastError());
    std::wstring inf = absolute;
    const size_t slash = inf.find_last_of(L"\\/");
    if (slash == std::wstring::npos || _wcsicmp(inf.substr(slash + 1).c_str(), L"Vdevice.inf") || !IsOwnInf(inf))
        return Failure(L"The package is not a Vdevice driver package", ERROR_INVALID_DATA);
    std::wstring catalog = inf.substr(0, slash + 1) + L"Vdevice.cat";
    LONG trust = CheckCatalog(catalog);
    if (trust != ERROR_SUCCESS) {
        std::wcerr << L"The Vdevice catalog is absent, untrusted, or cannot be verified. Submit this driver for Microsoft signing before installing on a standard Windows system. Security settings are not changed.\n";
        return Failure(L"Validate driver catalog", static_cast<DWORD>(trust));
    }
    BOOL reboot = FALSE;
    std::wcout << L"Installing verified driver package...\n";
    DWORD stageError = StagePackage(inf, reboot);
    if (stageError != ERROR_SUCCESS) return Failure(L"Stage driver package for reinstall", stageError);
    DeviceSet existing; existing.value = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES);
    if (existing.value == INVALID_HANDLE_VALUE) return Failure(L"Enumerate existing devices", GetLastError());
    bool found = false;
    for (DWORD index = 0;; ++index) {
        SP_DEVINFO_DATA item = {}; item.cbSize = sizeof(item);
        if (!SetupDiEnumDeviceInfo(existing.value, index, &item)) {
            if (GetLastError() != ERROR_NO_MORE_ITEMS) return Failure(L"Enumerate existing devices", GetLastError());
            break;
        }
        if (Matches(existing.value, item)) found = true;
    }
    DeviceSet created; SP_DEVINFO_DATA device = {}; device.cbSize = sizeof(device); bool registered = false;
    if (!found) {
        GUID deviceClass = {}; wchar_t className[256] = {};
        if (!SetupDiGetINFClassW(inf.c_str(), &deviceClass, className, 256, nullptr)) return Failure(L"Read driver setup class", GetLastError());
        created.value = SetupDiCreateDeviceInfoList(&deviceClass, nullptr);
        if (created.value == INVALID_HANDLE_VALUE) return Failure(L"Create root device list", GetLastError());
        if (!SetupDiCreateDeviceInfoW(created.value, className, &deviceClass, L"Vdevice Virtual USB Controller", nullptr, DICD_GENERATE_ID, &device))
            return Failure(L"Create root device", GetLastError());
        const wchar_t hardware[] = L"ROOT\\Vdevice\0";
        if (!SetupDiSetDeviceRegistryPropertyW(created.value, &device, SPDRP_HARDWAREID,
                reinterpret_cast<const BYTE*>(hardware), sizeof(hardware)) ||
            !SetupDiCallClassInstaller(DIF_REGISTERDEVICE, created.value, &device)) return Failure(L"Register root device", GetLastError());
        registered = true;
    }
    BOOL bindReboot = FALSE;
    if (!UpdateDriverForPlugAndPlayDevicesW(nullptr, VD_HARDWARE_ID, inf.c_str(), INSTALLFLAG_FORCE, &bindReboot)) {
        DWORD error = GetLastError();
        if (registered) { BOOL cleanupReboot = FALSE; DiUninstallDevice(nullptr, created.value, &device, 0, &cleanupReboot); }
        return Failure(L"Bind Vdevice driver", error);
    }
    std::wcout << L"Windows completed the package-binding step; checking the actual SYS files...\n";
    int verification = Status(inf.substr(0, slash + 1) + L"Vdevice.sys");
    if (verification == ERROR_REVISION_MISMATCH) return verification;
    if (reboot || bindReboot) {
        std::wcout << L"Windows requires a restart. Run diagnostics after restarting to verify the active SYS before using virtual devices.\n";
        return ERROR_SUCCESS_REBOOT_REQUIRED;
    }
    if (verification == ERROR_SUCCESS) std::wcout << L"Vdevice driver package and active root controller installed and verified.\n";
    return verification;
}
int Uninstall() {
    DWORD quiesce = QuiesceDriver();
    if (quiesce != ERROR_SUCCESS) return Failure(L"Prepare the driver for removal", quiesce);
    std::wcout << L"Enumerating only Vdevice root controllers and their owned packages...\n";
    DeviceSet set; set.value = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES);
    if (set.value == INVALID_HANDLE_VALUE) return Failure(L"Enumerate Vdevice controllers", GetLastError());
    std::vector<SP_DEVINFO_DATA> devices; std::set<std::wstring> packages;
    for (DWORD index = 0;; ++index) {
        SP_DEVINFO_DATA item = {}; item.cbSize = sizeof(item);
        if (!SetupDiEnumDeviceInfo(set.value, index, &item)) {
            if (GetLastError() != ERROR_NO_MORE_ITEMS) return Failure(L"Enumerate Vdevice controllers", GetLastError());
            break;
        }
        if (!Matches(set.value, item)) continue;
        devices.push_back(item); std::wstring inf = Lower(InfName(set.value, item));
        if (inf.size() > 7 && inf.find(L"oem") == 0 && inf.substr(inf.size() - 4) == L".inf"
            && inf.find_first_not_of(L"0123456789", 3) == inf.size() - 4) packages.insert(inf);
    }
    bool reboot = false;
    for (auto& item : devices) {
        wchar_t instance[MAX_DEVICE_ID_LEN] = {};
        SetupDiGetDeviceInstanceIdW(set.value, &item, instance, MAX_DEVICE_ID_LEN, nullptr);
        std::wcout << L"Requesting Windows PnP removal: " << instance << L". Windows may require a restart if applications retain controller handles.\n";
        BOOL needed = FALSE;
        if (!DiUninstallDevice(nullptr, set.value, &item, 0, &needed)) return Failure(L"Remove Vdevice root controller", GetLastError());
        reboot = reboot || needed != FALSE;
        std::wcout << L"PnP removal request completed; restart required: " << (needed ? L"yes" : L"no") << L"\n";
    }
    wchar_t windows[MAX_PATH] = {};
    if (!GetWindowsDirectoryW(windows, MAX_PATH)) return Failure(L"Find Windows INF directory", GetLastError());
    // An earlier failed install can leave a staged package without any root
    // device. Include those owned packages while leaving unrelated INFs alone.
    WIN32_FIND_DATAW found = {};
    std::wstring infDirectory = std::wstring(windows) + L"\\INF\\";
    HANDLE search = FindFirstFileW((infDirectory + L"oem*.inf").c_str(), &found);
    if (search != INVALID_HANDLE_VALUE) {
        do {
            if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && IsOwnInf(infDirectory + found.cFileName)) packages.insert(Lower(found.cFileName));
        } while (FindNextFileW(search, &found));
        FindClose(search);
    }
    for (const auto& inf : packages) {
        std::wstring full = std::wstring(windows) + L"\\INF\\" + inf;
        if (!IsOwnInf(full)) { std::wcout << L"Package retained: ownership could not be confirmed for " << inf << L"\n"; continue; }
        std::wcout << L"Removing owned driver package: " << inf << L"\n";
        if (!SetupUninstallOEMInfW(inf.c_str(), 0, nullptr)) {
            DWORD error = GetLastError();
            if (reboot && error == ERROR_INF_IN_USE_BY_DEVICES) {
                std::wcout << L"The package remains in use until Windows restarts. If package cleanup is still needed after restarting, run /uninstall again. No Driver Store files were forcibly deleted.\n";
                continue;
            }
            return Failure(L"Remove Vdevice driver package", error);
        }
        std::wcout << L"Removed package: " << inf << L"\n";
    }
    std::wcout << L"Removed Vdevice root controllers: " << devices.size() << L"\n";
    if (reboot) { std::wcout << L"A Windows restart is required.\n"; return ERROR_SUCCESS_REBOOT_REQUIRED; }
    return 0;
}
}
int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_U8TEXT);
    _setmode(_fileno(stderr), _O_U8TEXT);
    std::wcout << std::unitbuf;
    std::wcerr << std::unitbuf;
    try {
    if ((argc == 2 || argc == 3) && (_wcsicmp(argv[1], L"/status") == 0 || _wcsicmp(argv[1], L"/diagnose") == 0)) return Status(argc == 3 ? argv[2] : L"");
    if (argc == 2 && _wcsicmp(argv[1], L"/uninstall") == 0) return Uninstall();
    if (argc == 3 && _wcsicmp(argv[1], L"/install") == 0) return Install(argv[2]);
    std::wcout << L"Vdevice native setup helper\n/install <Vdevice.inf> | /uninstall | /status [expected Vdevice.sys]\n";
    return argc == 2 && (wcscmp(argv[1], L"/?") == 0 || wcscmp(argv[1], L"--help") == 0) ? 0 : ERROR_INVALID_PARAMETER;
    } catch (const std::exception&) { return Failure(L"Setup could not allocate or process its working data", ERROR_NOT_ENOUGH_MEMORY); }
}
