// VR headsets and their controllers, read through SteamVR (OpenVR).
//
// Whatever brings the headset into SteamVR (Valve Index, Vive, Quest over ALVR / Virtual Desktop /
// Steam Link / Quest Link, Pico ...) publishes the battery of the headset and of each controller as
// tracked-device properties. This driver asks SteamVR for them while it runs and never starts it:
//  - nothing is loaded unless vrserver.exe is running;
//  - vrclient_x64.dll comes from the SteamVR install named in %LOCALAPPDATA%\openvr\openvrpaths.vrpath
//    (what openvr_api.dll does), so no OpenVR DLL ships with the app;
//  - the connection is a "background" application (SteamVR is never launched for it) and is closed
//    after every poll, so quitting SteamVR never waits on this app.
// Interface layouts follow ValveSoftware/openvr: src/ivrclientcore.h (IVRClientCore_003) and
// headers/openvr_capi.h (VR_IVRSystem_FnTable, IVRSystem_026).
#include "drivers.h"

#include <tlhelp32.h>

#include <cstdint>
#include <cwctype>
#include <string>

#include "../platform_compat.h"

namespace {

// --- OpenVR ABI (only what is used) -------------------------------------------------------------

constexpr int kAppBackground = 3;              // EVRApplicationType::VRApplication_Background
constexpr uint32_t kMaxDevices = 64;           // k_unMaxTrackedDeviceCount
constexpr int kClassHmd = 1, kClassController = 2, kClassTracker = 3;  // ETrackedDeviceClass
constexpr int kRoleLeft = 1, kRoleRight = 2;   // ETrackedControllerRole
constexpr int kPropModel = 1001;               // Prop_ModelNumber_String
constexpr int kPropSerial = 1002;              // Prop_SerialNumber_String
constexpr int kPropCharging = 1011;            // Prop_DeviceIsCharging_Bool
constexpr int kPropBattery = 1012;             // Prop_DeviceBatteryPercentage_Float (0..1)
constexpr int kPropHasBattery = 1026;          // Prop_DeviceProvidesBatteryStatus_Bool

class IVRClientCore {  // vtable order of IVRClientCore_003
public:
    virtual int Init(int appType, const char* startupInfo) = 0;
    virtual void Cleanup() = 0;
    virtual int IsInterfaceVersionValid(const char* version) = 0;
    virtual void* GetGenericInterface(const char* nameAndVersion, int* error) = 0;
    virtual bool BIsHmdPresent() = 0;
    virtual const char* GetEnglishStringForHmdError(int error) = 0;
    virtual const char* GetIDForVRInitError(int error) = 0;
};
using ClientCoreFactory = void* (*)(const char* interfaceName, int* returnCode);

#define VRCALL __stdcall
struct SystemFnTable {  // VR_IVRSystem_FnTable for IVRSystem_026, up to the property getters
    void* GetRecommendedRenderTargetSize;
    void* GetProjectionMatrix;
    void* GetProjectionRaw;
    void* ComputeDistortion;
    void* ComputeDistortionSet;
    void* GetEyeToHeadTransform;
    void* GetTimeSinceLastVsync;
    void* GetD3D9AdapterIndex;
    void* GetDXGIOutputInfo;
    void* GetOutputDevice;
    void* IsDisplayOnDesktop;
    void* SetDisplayVisibility;
    void* GetDeviceToAbsoluteTrackingPose;
    void* GetSeatedZeroPoseToStandingAbsoluteTrackingPose;
    void* GetRawZeroPoseToStandingAbsoluteTrackingPose;
    void* GetSortedTrackedDeviceIndicesOfClass;
    void* GetTrackedDeviceActivityLevel;
    void* ApplyTransform;
    void* GetTrackedDeviceIndexForControllerRole;
    int(VRCALL* GetControllerRoleForTrackedDeviceIndex)(uint32_t device);
    int(VRCALL* GetTrackedDeviceClass)(uint32_t device);
    bool(VRCALL* IsTrackedDeviceConnected)(uint32_t device);
    bool(VRCALL* GetBoolTrackedDeviceProperty)(uint32_t device, int prop, int* error);
    float(VRCALL* GetFloatTrackedDeviceProperty)(uint32_t device, int prop, int* error);
    void* GetInt32TrackedDeviceProperty;
    void* GetUint64TrackedDeviceProperty;
    void* GetMatrix34TrackedDeviceProperty;
    void* GetArrayTrackedDeviceProperty;
    uint32_t(VRCALL* GetStringTrackedDeviceProperty)(uint32_t device, int prop, char* value, uint32_t size, int* error);
};
#undef VRCALL

// --- Finding SteamVR ------------------------------------------------------------------------------

bool ProcessRunning(const wchar_t* exe) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    bool found = false;
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !found; ok = Process32NextW(snap, &pe))
        found = _wcsicmp(pe.szExeFile, exe) == 0;
    CloseHandle(snap);
    return found;
}

// First entry of "runtime" in %LOCALAPPDATA%\openvr\openvrpaths.vrpath (a small JSON file).
std::wstring RuntimePath() {
    wchar_t base[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", base, MAX_PATH);
    if (!n || n >= MAX_PATH) return L"";
    HANDLE f = CreateFileW((std::wstring(base) + L"\\openvr\\openvrpaths.vrpath").c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return L"";
    std::string json;
    char buf[4096];
    DWORD got = 0;
    while (json.size() < (1u << 20) && ReadFile(f, buf, sizeof(buf), &got, nullptr) && got) json.append(buf, got);
    CloseHandle(f);
    size_t p = json.find("\"runtime\"");
    if (p == std::string::npos || (p = json.find('[', p)) == std::string::npos) return L"";
    if ((p = json.find('"', p)) == std::string::npos) return L"";
    std::string path;
    for (++p; p < json.size() && json[p] != '"'; ++p) {
        if (json[p] == '\\' && p + 1 < json.size()) ++p;  // \\ and \/ are the only escapes in a path
        path += json[p];
    }
    return Utf8ToWide(path);
}

IVRClientCore* ClientCore() {
    static IVRClientCore* core = nullptr;
    if (core) return core;
    std::wstring runtime = RuntimePath();
    if (runtime.empty()) return nullptr;
#ifdef _WIN64
    std::wstring dll = runtime + L"\\bin\\vrclient_x64.dll";
#else
    std::wstring dll = runtime + L"\\bin\\vrclient.dll";
#endif
    // Kept loaded for the life of the app: SteamVR comes and goes, its client library does not need to.
    HMODULE mod = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!mod) return nullptr;
    auto factory = (ClientCoreFactory)(void*)GetProcAddress(mod, "VRClientCoreFactory");
    int rc = 0;
    if (factory) core = static_cast<IVRClientCore*>(factory("IVRClientCore_003", &rc));
    if (!core) FreeLibrary(mod);
    return core;
}

// --- Reading the devices --------------------------------------------------------------------------

std::wstring StringProp(SystemFnTable* sys, uint32_t i, int prop) {
    char buf[256] = {};
    int err = 0;
    uint32_t n = sys->GetStringTrackedDeviceProperty(i, prop, buf, sizeof(buf), &err);
    if (err || !n) return L"";
    buf[sizeof(buf) - 1] = 0;
    return Utf8ToWide(buf);
}

bool Mentions(const std::wstring& s, const wchar_t* word) {
    std::wstring a = s, b = word;
    for (auto& c : a) c = (wchar_t)towlower(c);
    for (auto& c : b) c = (wchar_t)towlower(c);
    return a.find(b) != std::wstring::npos;
}

void ReadDevices(SystemFnTable* sys, PollContext& ctx) {
    for (uint32_t i = 0; i < kMaxDevices; i++) {
        int cls = sys->GetTrackedDeviceClass(i);
        if (cls != kClassHmd && cls != kClassController && cls != kClassTracker) continue;
        if (!sys->IsTrackedDeviceConnected(i)) continue;
        int err = 0;
        if (!sys->GetBoolTrackedDeviceProperty(i, kPropHasBattery, &err) || err) continue;  // wired headset, base station...
        float level = sys->GetFloatTrackedDeviceProperty(i, kPropBattery, &err);
        if (err || level < 0.f) continue;
        bool charging = sys->GetBoolTrackedDeviceProperty(i, kPropCharging, &err) && !err;

        std::wstring model = StringProp(sys, i, kPropModel);
        std::wstring serial = StringProp(sys, i, kPropSerial);
        int role = cls == kClassHmd ? 0 : sys->GetControllerRoleForTrackedDeviceIndex(i);
        const wchar_t* side = role == kRoleLeft ? L"Left" : role == kRoleRight ? L"Right" : nullptr;

        DeviceStatus s;
        if (cls == kClassHmd) {
            s.kind = DeviceKind::VrHeadset;
            s.name = model.empty() ? L"VR headset" : model;
        } else {
            s.kind = DeviceKind::Gamepad;
            s.name = model.empty() ? (cls == kClassTracker ? L"VR tracker" : L"VR controller") : model;
            // "Knuckles Left" or "Oculus Quest2 (Left Controller)" already say which hand.
            if (side && !Mentions(s.name, side)) s.name += std::wstring(L" (") + side + L")";
        }
        // The serial follows the device; the slot index can change when SteamVR restarts.
        std::wstring id = !serial.empty() ? serial
                                          : (cls == kClassHmd ? std::wstring(L"hmd") : std::wstring(side ? side : L"device") + L"-" + std::to_wstring(i));
        s.key = ctx.StableKey(L"steamvr:" + id);
        s.via = L"SteamVR";
        s.percent = Clamp100((int)(level * 100.f + 0.5f));
        s.charging = charging;
        s.full = charging && s.percent >= 100;
        s.graceSec = 120;  // controllers sleep when put down: keep the last level for a moment
        ctx.out.push_back(s);
    }
}

}  // namespace

void PollSteamVR(PollContext& ctx) {
    if (!ProcessRunning(L"vrserver.exe")) return;  // SteamVR is not running: load nothing
    IVRClientCore* core = ClientCore();
    if (!core) return;
    if (core->Init(kAppBackground, nullptr) != 0) return;  // e.g. NoServerForBackgroundApp while SteamVR shuts down
    int err = 0;
    auto* sys = static_cast<SystemFnTable*>(core->GetGenericInterface("FnTable:IVRSystem_026", &err));
    if (sys && !err) ReadDevices(sys, ctx);
    core->Cleanup();
}
