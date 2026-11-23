// Smartphones (iPhone, Android) paired with this PC over Bluetooth.
//
// Windows' Bluetooth fallback (PollBluetooth) only lists a device when Windows itself stores a level for
// it, which for a phone happens only while its Hands-Free link to the PC is up (the phone sends its
// battery indicator to the PC acting as a hands-free unit). Most of the time a paired phone has no level
// there, so it was missing from the app. This driver lists phones on their own:
//
//  1. Windows' level (the same value Settings > Bluetooth & devices shows), when there is one.
//  2. Otherwise the standard LE Battery Service (0x180F / Battery Level 0x2A19). iPhones expose it to
//     the computers they are paired with (that is how watches and car kits read it); some Android phones
//     do too. Only Battery Level is read: no writes, no notifications. Connecting takes seconds, so the
//     read runs on its own thread at most every two minutes and the poll reports the cached value, like
//     marshall.cpp. A phone that never answers is asked less and less often (up to every 15 minutes).
//  3. A paired phone that gives no level either way is still listed in the menu under
//     "No battery info", so the user sees that it was found (unless Phone Link shows a phone).
//
// iPhone or Android is told by the Bluetooth Device ID vendor on its service nodes
// ("_VID&0001004C" = Apple, Bluetooth SIG company id 0x004C), then by the name. The icon is Windows' own
// "CellPhone" glyph from the Segoe Fluent Icons font (widget.cpp), the one Settings uses for phones;
// the brand tint (silver for iPhone, Android green otherwise) comes from brands.cpp.
#include "drivers.h"
#include "ble_adverts.h"
#include "bluetooth_identity.h"
#include "bluetooth_props.h"

#include <setupapi.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <devpkey.h>
#include <roapi.h>

#include <cwctype>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace {

using btprops::kClassOfDevice;
using btprops::kIsConnected;
using btprops::StripServiceSuffix;
const DEVPROPKEY& kBtBattery = btprops::kBattery;

const ULONGLONG kQueryEveryMs = 120 * 1000;      // after a successful read
const ULONGLONG kRetryMs = 60 * 1000;            // first retry after a failed read, doubled after each failure
const ULONGLONG kMaxRetryMs = 15 * 60 * 1000;    // a phone that keeps failing (no Battery Service)
const ULONGLONG kStaleMs = 6 * 60 * 1000;        // stop reporting a reading this old

constexpr bool IsPhoneClass(UINT32 cod) { return ((cod >> 8) & 0x1F) == 0x02; }
static_assert(IsPhoneClass(0x7A020C));   // iPhone
static_assert(IsPhoneClass(0x5A020C));   // Android smartphone
static_assert(!IsPhoneClass(0x240404));  // headset

bool IsPhoneCategory(const std::wstring& c) { return c == L"Communication.Phone" || c.rfind(L"Communication.Phone.", 0) == 0; }

std::wstring Lower(std::wstring s) {
    for (auto& c : s) c = (wchar_t)towlower(c);
    return s;
}

struct Phone {
    std::wstring cid;          // device container id, "{...}"
    std::wstring name;
    bool phone = false;        // class of device or container category says phone
    bool apple = false;
    bool samsung = false;
    bool connected = false;   // a main (BTHENUM\DEV_ / BTHLE\DEV_) node is connected
    UINT64 classicAddr = 0;    // BTHENUM\DEV_ address (public)
    UINT64 leAddr = 0;         // BTHLE\DEV_ address (identity address of the LE pairing)
    int winPct = -1;           // Windows' own level
    bool winLive = false;      // that level comes from a connected node
    std::vector<std::wstring> twins;  // containers of the same phone merged into this one (MergeLeTwins)
};

// Name for comparing: lower case, typographic apostrophe as a plain one ("Alex’s iPhone").
std::wstring NameKey(const std::wstring& s) {
    std::wstring n = Lower(s);
    for (auto& c : n)
        if (c == L'’') c = L'\'';
    return n;
}

// One phone can show up as two Bluetooth containers: the classic pairing ("Alex's iPhone",
// BTHENUM) and a separate LE pairing with only the model name ("iPhone", BTHLE). Each one was listed,
// so the phone appeared twice. An LE-only container whose name is contained in the name of exactly one
// classic container of the same brand is folded into it. The classic one keeps its key (settings and
// battery history) and takes the LE address (Battery Service reads) and any level only the LE side had.
void MergeLeTwins(std::vector<Phone>& phones) {
    for (size_t i = 0; i < phones.size();) {
        Phone& le = phones[i];
        size_t match = phones.size();
        int matches = 0;
        if (!le.classicAddr && le.leAddr) {
            std::wstring short_ = NameKey(le.name);
            for (size_t j = 0; j < phones.size(); j++) {
                const Phone& c = phones[j];
                if (j == i || !c.classicAddr || c.apple != le.apple) continue;
                if (short_.size() >= 4 && NameKey(c.name).find(short_) != std::wstring::npos) {
                    match = j;
                    matches++;
                }
            }
        }
        if (matches != 1) {  // none, or ambiguous (two phones of that model): keep both
            i++;
            continue;
        }
        Phone& c = phones[match];
        if (!c.leAddr) c.leAddr = le.leAddr;
        c.connected |= le.connected;
        c.samsung |= le.samsung;
        if (c.winPct < 0 || (le.winLive && !c.winLive)) {
            if (le.winPct >= 0) {
                c.winPct = le.winPct;
                c.winLive = le.winLive;
            }
        }
        c.twins.push_back(le.cid);
        for (auto& t : le.twins) c.twins.push_back(t);
        phones.erase(phones.begin() + i);  // `match` may shift, but it is not used after this
    }
}

std::vector<Phone> FindPhones() {
    std::map<std::wstring, Phone> by;
    HDEVINFO di = SetupDiGetClassDevsW(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (di == INVALID_HANDLE_VALUE) return {};
    SP_DEVINFO_DATA dd{ sizeof(dd) };
    for (DWORD i = 0; SetupDiEnumDeviceInfo(di, i, &dd); i++) {
        wchar_t inst[512];
        if (!SetupDiGetDeviceInstanceIdW(di, &dd, inst, 512, nullptr)) continue;
        if (wcsncmp(inst, L"BTHENUM\\", 8) && wcsncmp(inst, L"BTHLE\\", 6) && wcsncmp(inst, L"BTHLEDEVICE\\", 12)) continue;
        GUID container{};
        DEVPROPTYPE t;
        if (!SetupDiGetDevicePropertyW(di, &dd, &DEVPKEY_Device_ContainerId, &t, (PBYTE)&container, sizeof(container), nullptr, 0))
            continue;
        wchar_t g[64];
        StringFromGUID2(container, g, 64);
        Phone& p = by[g];
        p.cid = g;
        std::wstring id = inst;
        if (id.find(L"_VID&0001004C") != std::wstring::npos || id.find(L"_VID&000205AC") != std::wstring::npos) p.apple = true;
        if (id.find(L"_VID&00010075") != std::wstring::npos) p.samsung = true;  // Bluetooth SIG id of Samsung (seen on a Galaxy S26)

        bool classicMain = !wcsncmp(inst, L"BTHENUM\\DEV_", 12), leMain = !wcsncmp(inst, L"BTHLE\\DEV_", 10);
        DEVPROP_BOOLEAN conn = DEVPROP_FALSE;
        bool hasConn = SetupDiGetDevicePropertyW(di, &dd, &kIsConnected, &t, (PBYTE)&conn, sizeof(conn), nullptr, 0) &&
                       t == DEVPROP_TYPE_BOOLEAN;
        if (classicMain || leMain) {
            std::wstring name = btprops::String(di, &dd, DEVPKEY_Device_FriendlyName);
            if (!name.empty() && (p.name.empty() || classicMain)) p.name = name;
            if (UINT64 a = btprops::Address(inst)) (classicMain ? p.classicAddr : p.leAddr) = a;
            UINT32 cod = 0;
            if (SetupDiGetDevicePropertyW(di, &dd, &kClassOfDevice, &t, (PBYTE)&cod, sizeof(cod), nullptr, 0) &&
                t == DEVPROP_TYPE_UINT32 && IsPhoneClass(cod))
                p.phone = true;
            if (IsPhoneCategory(btprops::String(di, &dd, DEVPKEY_DeviceContainer_Category))) p.phone = true;
            if (hasConn && conn != DEVPROP_FALSE) p.connected = true;
        }

        BYTE pct = 0;
        if (SetupDiGetDevicePropertyW(di, &dd, &kBtBattery, &t, &pct, sizeof(pct), nullptr, 0) && t == DEVPROP_TYPE_BYTE && pct <= 100) {
            ULONG st = 0, prob = 0;
            if (CM_Get_DevNode_Status(&st, &prob, dd.DevInst, 0) == CR_SUCCESS && (st & DN_HAS_PROBLEM)) continue;
            bool live = !hasConn || conn != DEVPROP_FALSE;
            if (p.winPct < 0 || (live && !p.winLive)) {
                p.winPct = pct;
                p.winLive = live;
            }
            if (p.name.empty()) p.name = StripServiceSuffix(btprops::String(di, &dd, DEVPKEY_Device_FriendlyName));
        }
    }
    SetupDiDestroyDeviceInfoList(di);

    std::vector<Phone> out;
    for (auto& [cid, p] : by) {
        if (!p.phone || (!p.classicAddr && !p.leAddr)) continue;
        if (Lower(p.name).find(L"iphone") != std::wstring::npos) p.apple = true;
        if (p.name.empty()) p.name = p.apple ? L"iPhone" : L"Phone";
        out.push_back(p);
    }
    MergeLeTwins(out);
    return out;
}

// ---- background Battery Service reads ----

struct Entry {
    ULONGLONG lastAttempt = 0;
    ULONGLONG lastOk = 0;
    int failures = 0;
    int percent = -1;
};

struct Job { std::wstring cid; UINT64 leAddr, classicAddr; };

struct State {
    std::mutex m;
    bool running = false;
    std::map<std::wstring, Entry> entries;  // by container id
    std::vector<Job> pending;
};
State g_state;

ULONGLONG RetryDelay(const Entry& e) {
    if (e.lastOk && e.lastOk >= e.lastAttempt) return kQueryEveryMs;
    ULONGLONG d = kRetryMs << (e.failures > 4 ? 4 : e.failures);
    return d > kMaxRetryMs ? kMaxRetryMs : d;
}

DWORD WINAPI QueryThread(LPVOID) {
    std::vector<Job> jobs;
    {
        std::lock_guard<std::mutex> lock(g_state.m);
        jobs.swap(g_state.pending);
    }
    HRESULT init = RoInitialize(RO_INIT_MULTITHREADED);
    for (const Job& j : jobs) {
        int level = -1;
        // The LE pairing's identity address first (public on iPhones, which share it with Classic),
        // then the Classic address, which on most phones is the same public address.
        if (j.leAddr) level = ReadLeBatteryLevel(j.leAddr, false, false);
        if (level < 0 && j.leAddr) level = ReadLeBatteryLevel(j.leAddr, true, false);
        if (level < 0 && j.classicAddr && j.classicAddr != j.leAddr) level = ReadLeBatteryLevel(j.classicAddr, false, false);
        std::lock_guard<std::mutex> lock(g_state.m);
        Entry& e = g_state.entries[j.cid];
        if (level >= 0) {
            e.percent = level;
            e.lastOk = GetTickCount64();
            e.failures = 0;
        } else {
            e.failures++;
        }
    }
    if (SUCCEEDED(init)) RoUninitialize();
    std::lock_guard<std::mutex> lock(g_state.m);
    g_state.running = false;
    return 0;
}

}  // namespace

void PollPhones(PollContext& ctx) {
    std::vector<Phone> phones = FindPhones();
    if (phones.empty()) return;

    // Phones without a live Windows level get a Battery Service read on the query thread.
    HANDLE first = nullptr;
    {
        ULONGLONG now = GetTickCount64();
        std::lock_guard<std::mutex> lock(g_state.m);
        if (!g_state.running) {
            std::vector<Job> due;
            bool isFirst = false;
            for (const Phone& p : phones) {
                if (p.winPct >= 0 && p.winLive) continue;
                Entry& e = g_state.entries[p.cid];
                if (e.lastAttempt && now - e.lastAttempt < RetryDelay(e)) continue;
                isFirst |= !e.lastAttempt;
                e.lastAttempt = now;
                due.push_back({ p.cid, p.leAddr, p.classicAddr });
            }
            if (!due.empty()) {
                EnsureBleWatcher();  // poll thread only (not thread-safe); lets Windows hear the phone's LE side
                g_state.pending = due;
                if (HANDLE t = CreateThread(nullptr, 0, QueryThread, nullptr, 0, nullptr)) {
                    g_state.running = true;
                    if (isFirst) first = t;
                    else CloseHandle(t);
                }
            }
        }
    }
    if (first) {
        // Only the very first read is waited for (app start, --json), with one 4 s deadline.
        WaitForSingleObject(first, 4000);
        CloseHandle(first);
    }

    std::lock_guard<std::mutex> lock(g_state.m);
    ULONGLONG now = GetTickCount64();
    for (const Phone& p : phones) {
        DeviceStatus s;
        s.key = L"bt:" + p.cid;  // same key as the Bluetooth fallback: settings and history carry over
        s.name = p.name;
        s.via = L"Bluetooth";
        s.kind = DeviceKind::Phone;
        s.vid = p.apple ? 0x004C : p.samsung ? 0x04E8 : 0;  // brand tint (brands.cpp): Apple, Samsung
        s.detail = p.apple ? L"iPhone (iOS)" : L"Android";
        s.graceSec = 300;
        auto it = g_state.entries.find(p.cid);
        bool fresh = it != g_state.entries.end() && it->second.lastOk && now - it->second.lastOk < kStaleMs;
        if (p.winPct >= 0 && p.winLive) s.percent = p.winPct;
        else if (fresh) s.percent = it->second.percent;
        else if (p.winPct >= 0 && p.connected) s.percent = p.winPct;
        if (IsDuplicateBluetoothDevice(s, ctx.out)) continue;  // e.g. already shown by Phone Link under its name
        if (!p.classicAddr) {
            // LE-only "iPhone" while Phone Link already shows "Alex's iPhone": the same phone.
            bool shown = false;
            std::wstring n = NameKey(s.name);
            for (const auto& o : ctx.out)
                shown |= o.kind == DeviceKind::Phone && n.size() >= 4 && NameKey(o.name).find(n) != std::wstring::npos;
            if (shown) {
                ctx.btContainers.insert(p.cid);
                continue;
            }
        }
        if (s.percent < 0) {
            // Paired, no level (Windows only connects a phone during calls or audio, and most Android
            // phones have no Battery Service): menu only, under "No battery info", so the user sees it
            // was found. Not when Phone Link already shows a phone with its level (often the same one
            // under another name, e.g. "Galaxy S26" there and "S26 de ..." in Bluetooth).
            bool phoneLink = false;
            for (const auto& o : ctx.out) phoneLink |= o.key == L"phonelink";
            if (phoneLink) continue;
            s.unsupported = true;
        }
        ctx.out.push_back(s);
        ctx.btContainers.insert(p.cid);
        for (const auto& t : p.twins) ctx.btContainers.insert(t);  // so the Bluetooth fallback skips them too
    }
}
