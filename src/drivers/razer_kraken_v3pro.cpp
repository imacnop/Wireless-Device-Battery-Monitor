// Razer Kraken V3 Pro headset on its HyperSpeed dongle (1532:052C). Its HID interface only carries
// media keys; settings go over the dongle's USB serial port (interface 4, 9600 8N1) in HCI-style frames:
//   request 01 F3 FF 18 02 20 24 00 ...                28 bytes: vendor command FFF3, read (20), status (24)
//   reply   04 0E 1C 01 F3 FF 00 02 20 24 V0 V1 V2 V3  V0 bit 2 = charging, V3 = level % (10 % steps)
//   events  04 FF 01 E8 / E9                           headset link up / down
// A headset that is off does not answer. The port has one owner, so it is left alone while Razer
// Synapse runs. From razer-peripherals-protocol (razer-kraken-v3-pro.md, Synapse captures) and
// OmaKraken (MIT), as used by ferloom/Wireless-Device-Battery-Monitor; untested here.
#include "drivers.h"

#include <setupapi.h>
#include <initguid.h>
#include <devguid.h>
#include <tlhelp32.h>

#include <cwctype>
#include <string>

namespace {

const USHORT kVid = 0x1532;
const USHORT kPid = 0x052C;

std::vector<std::wstring> FindPorts() {
    std::vector<std::wstring> out;
    HDEVINFO di = SetupDiGetClassDevsW(&GUID_DEVCLASS_PORTS, nullptr, nullptr, DIGCF_PRESENT);
    if (di == INVALID_HANDLE_VALUE) return out;
    SP_DEVINFO_DATA dd{ sizeof(dd) };
    for (DWORD i = 0; SetupDiEnumDeviceInfo(di, i, &dd); i++) {
        wchar_t inst[512];
        if (!SetupDiGetDeviceInstanceIdW(di, &dd, inst, 512, nullptr)) continue;
        std::wstring id = inst;
        for (auto& ch : id) ch = towupper(ch);
        if (id.find(L"VID_1532&PID_052C") == std::wstring::npos) continue;
        HKEY k = SetupDiOpenDevRegKey(di, &dd, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (k == INVALID_HANDLE_VALUE) continue;
        wchar_t name[32] = {};
        DWORD sz = sizeof(name) - sizeof(wchar_t), type = 0;
        if (RegQueryValueExW(k, L"PortName", nullptr, &type, (BYTE*)name, &sz) == ERROR_SUCCESS && type == REG_SZ)
            out.push_back(name);
        RegCloseKey(k);
    }
    SetupDiDestroyDeviceInfoList(di);
    return out;
}

bool SynapseRunning() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return true;  // unsure: do not take the port
    PROCESSENTRY32W e{ sizeof(e) };
    bool found = false;
    for (BOOL more = Process32FirstW(snap, &e); more && !found; more = Process32NextW(snap, &e)) {
        std::wstring exe = e.szExeFile;
        for (auto& ch : exe) ch = towlower(ch);
        found = exe.rfind(L"razer synapse", 0) == 0;
    }
    CloseHandle(snap);
    return found;
}

// Looks for the status reply in the byte stream.
bool FindStatus(const std::vector<BYTE>& buf, int& level, bool& charging) {
    size_t i = 0;
    while (i < buf.size()) {
        if (buf[i] != 0x04) { i++; continue; }
        if (i + 2 >= buf.size()) return false;
        size_t end = i + 3 + buf[i + 2];
        if (end > buf.size()) return false;
        const BYTE* p = buf.data() + i;
        if (end - i >= 14 && p[1] == 0x0E && p[2] == 0x1C && p[3] == 0x01 && p[4] == 0xF3 && p[5] == 0xFF &&
            p[7] == 0x02 && p[8] == 0x20 && p[9] == 0x24 && p[13] <= 100) {
            level = p[13];
            charging = (p[10] & 0x04) != 0;
            return true;
        }
        i = end;
    }
    return false;
}

bool Query(const std::wstring& port, int& level, bool& charging) {
    HANDLE h = CreateFileW((L"\\\\.\\" + port).c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DCB dcb{ sizeof(dcb) };
    GetCommState(h, &dcb);
    dcb.BaudRate = CBR_9600;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = TRUE;
    dcb.fOutxCtsFlow = dcb.fOutxDsrFlow = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    dcb.fOutX = dcb.fInX = FALSE;
    COMMTIMEOUTS to{};
    to.ReadIntervalTimeout = 50;
    to.ReadTotalTimeoutConstant = 200;
    to.WriteTotalTimeoutConstant = 300;
    bool found = false;
    if (SetCommState(h, &dcb) && SetCommTimeouts(h, &to)) {
        PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
        BYTE req[28] = { 0x01, 0xF3, 0xFF, 0x18, 0x02, 0x20, 0x24 };
        DWORD n = 0;
        if (WriteFile(h, req, sizeof(req), &n, nullptr) && n == sizeof(req)) {
            std::vector<BYTE> buf;
            ULONGLONG end = GetTickCount64() + 700;
            while (!found && (LONGLONG)end - (LONGLONG)GetTickCount64() > 0 && buf.size() < 4096) {
                BYTE chunk[128];
                if (!ReadFile(h, chunk, sizeof(chunk), &n, nullptr)) break;
                buf.insert(buf.end(), chunk, chunk + n);
                found = FindStatus(buf, level, charging);
            }
        }
    }
    CloseHandle(h);
    return found;
}

}  // namespace

void PollRazerKrakenV3Pro(PollContext& ctx) {
    // The HID interface shows the dongle is plugged in; only then are serial ports looked at.
    if (ctx.IsClaimed(kVid, kPid) || ctx.Find(kVid, kPid, [](const HidColl&) { return true; }).empty()) return;
    if (SynapseRunning()) return;
    for (const std::wstring& port : FindPorts()) {
        DeviceStatus s;
        s.key = L"razer:kraken-v3-pro";
        s.name = L"Razer Kraken V3 Pro";
        s.kind = DeviceKind::Headset;
        s.via = L"2.4 GHz receiver";
        s.graceSec = 300;
        int level = -1;
        bool charging = false;
        if (Query(port, level, charging)) {
            s.percent = level;
            s.charging = charging;
        } else {
            s.connected = false;  // no reply: the headset is off
        }
        ctx.Add(s, kVid, kPid);
        return;
    }
}
