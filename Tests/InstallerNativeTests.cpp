// These tests call only pure/read-only setup helpers. No installation entry
// point, device removal, registry mutation, or elevation is performed.
#define wmain VdeviceSetupMainForTests
#include "../InstallerConsole/Native/Setup.cpp"
#undef wmain
#include <filesystem>

namespace {
bool Write(const std::filesystem::path& path, const std::string& content) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << content; return stream.good();
}
DWORD installResult = ERROR_SUCCESS, copyResult = ERROR_SUCCESS, observedFlags = 0;
unsigned copies = 0;
BOOL WINAPI FakeInstall(HWND, PCWSTR, DWORD flags, PBOOL reboot) {
    observedFlags = flags; *reboot = TRUE; SetLastError(installResult); return installResult == ERROR_SUCCESS;
}
BOOL WINAPI FakeCopy(PCWSTR, PCWSTR, DWORD media, DWORD flags, PWSTR destination, DWORD size, PDWORD, PWSTR*) {
    ++copies;
    if (media != SPOST_PATH || flags || size < 9) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    wcscpy_s(destination, size, L"oem1.inf"); SetLastError(copyResult); return copyResult == ERROR_SUCCESS;
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 2 || !IsOwnInf(argv[1])) { std::wcerr << L"The current Vdevice INF was not recognized.\n"; return 1; }
    wchar_t directory[MAX_PATH] = {}, file[MAX_PATH] = {};
    if (!GetTempPathW(MAX_PATH, directory) || !GetTempFileNameW(directory, L"vdt", 0, file)) return 1;
    std::filesystem::path fixture(file);
    const std::string valid =
        "[Version]\r\nSignature=\"$WINDOWS NT$\"\r\nProvider=TitanCSA\r\nCatalogFile=Vdevice.cat\r\n"
        "[Models.NTamd64]\r\nDevice=Install,ROOT\\Vdevice\r\n"
        "[Install.NT.Services]\r\nAddService=Vdevice,2,Service\r\n";
    int failures = 0;
    wchar_t windows[MAX_PATH] = {};
    if (!GetWindowsDirectoryW(windows, MAX_PATH)) return 1;
    const std::wstring repository = std::wstring(windows) + L"\\System32\\DriverStore\\FileRepository\\";
    if (!OwnServiceImage(repository + L"vdevice.inf_amd64_abcd\\Vdevice.sys")) ++failures;
    if (!OwnServiceImage(L"\\SystemRoot\\System32\\DriverStore\\FileRepository\\vdevice.inf_amd64_abcd\\Vdevice.sys")) ++failures;
    if (OwnServiceImage(repository + L"other.inf_amd64_abcd\\Vdevice.sys")) ++failures;
    if (OwnServiceImage(repository + L"vdevice.inf_amd64_abcd\\Other.sys")) ++failures;
    if (OwnServiceImage(repository + L"vdevice.inf_amd64_abcd\\..\\Other\\Vdevice.sys")) ++failures;
    if (OwnServiceImage(L"C:\\Users\\Public\\Vdevice.sys")) ++failures;
    auto check = [&](const std::string& text, bool expected) {
        if (!Write(fixture, text) || IsOwnInf(fixture.wstring()) != expected) ++failures;
    };
    check(valid, true);
    auto replace = [&](const std::string& from, const std::string& to) {
        std::string changed = valid; changed.replace(changed.find(from), from.size(), to); check(changed, false);
    };
    replace("Provider=TitanCSA", "Provider=Other");
    replace("CatalogFile=Vdevice.cat", "CatalogFile=Other.cat");
    replace("ROOT\\Vdevice", "ROOT\\Interceptor");
    replace("AddService=Vdevice", "AddService=Interceptor");
    check("This is not an INF.", false);
    if (CheckCatalog(fixture.wstring()) == ERROR_SUCCESS) ++failures;
    BOOL reboot = FALSE;
    if (StagePackage(L"fixture.inf", reboot, FakeInstall, FakeCopy) != ERROR_SUCCESS || !reboot || copies || observedFlags != DIIRFLAG_FORCE_INF) ++failures;
    installResult = ERROR_NO_MORE_ITEMS;
    if (StagePackage(L"fixture.inf", reboot, FakeInstall, FakeCopy) != ERROR_SUCCESS || copies != 1) ++failures;
    installResult = ERROR_ACCESS_DENIED;
    if (StagePackage(L"fixture.inf", reboot, FakeInstall, FakeCopy) != ERROR_ACCESS_DENIED || copies != 1) ++failures;
    installResult = ERROR_NO_MORE_ITEMS; copyResult = ERROR_INVALID_DATA;
    if (StagePackage(L"fixture.inf", reboot, FakeInstall, FakeCopy) != ERROR_INVALID_DATA || copies != 2) ++failures;
    std::wstring hash; DWORD hashError = 0;
    if (!Write(fixture, "abc") || !FileSha256(fixture.wstring(), hash, hashError)
        || hash != L"BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD") ++failures;
    ImageState configured; configured.sha256 = L"A";
    ImageState loaded; loaded.sha256 = L"A";
    if (CompareImages(L"A", configured, {loaded}) != ERROR_SUCCESS) ++failures;
    configured.sha256 = L"B";
    if (CompareImages(L"A", configured, {loaded}) != ERROR_REVISION_MISMATCH) ++failures;
    configured.sha256 = L"A"; loaded.sha256 = L"B";
    if (CompareImages(L"A", configured, {loaded}) != ERROR_SUCCESS_REBOOT_REQUIRED) ++failures;
    if (CompareImages(L"A", configured, {}) != ERROR_NOT_READY) ++failures;
    loaded.sha256.clear(); loaded.error = ERROR_ACCESS_DENIED;
    if (CompareImages(L"A", configured, {loaded}) != ERROR_ACCESS_DENIED) ++failures;
    configured.sha256.clear();
    if (CompareImages(L"A", configured, {}) != ERROR_NOT_FOUND) ++failures;
    configured.path = L"unreadable.sys"; configured.error = ERROR_ACCESS_DENIED;
    if (CompareImages(L"A", configured, {}) != ERROR_ACCESS_DENIED) ++failures;
    DeleteFileW(file);
    DeleteFileW(file);
    if (CheckCatalog(fixture.wstring()) == ERROR_SUCCESS) ++failures;
    std::wcout << L"Installer ownership/catalog regression failures: " << failures << L"\n";
    return failures ? 1 : 0;
}
