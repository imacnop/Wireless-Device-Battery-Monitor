// Switch Pro-compatible controllers (057E:2009), including the GameSir Tarantula Pro
// over its 2.4 GHz receiver in Switch mode.
// SDL's SDL_hidapi_switch.c documents byte 2 of input reports 0x21 and 0x30:
// bits 7..5 are the battery level (0..4) and bit 4 indicates charging.
//
// Full reports (0x30) only stream once Steam Input or another host has enabled them; on a bare
// Bluetooth connection the pad sends simple 0x3F reports, which carry no battery. So the driver
// asks for it: it sends the harmless "request device info" subcommand (0x02), which leaves the
// input mode untouched, and reads the battery from the 0x21 reply. When the pad cannot be opened
// for writing (another app holds it), it falls back to listening for 0x30 reports.
#include "drivers.h"

#include <atomic>

namespace {

// Output report 0x01: report ID, packet counter (0..15), 8 bytes of neutral rumble, subcommand.
std::vector<BYTE> DeviceInfoRequest() {
    static std::atomic<unsigned> counter{ 0 };
    const BYTE n = (BYTE)(++counter & 0x0F);
    return { 0x01, n, 0x00, 0x01, 0x40, 0x40, 0x00, 0x01, 0x40, 0x40, 0x02 };
}

// Waits for a report carrying the battery byte; returns the level (0..4) or -1.
int ReadLevel(HidDev& d, BYTE& status, bool acceptReply, int waitMs = 350) {
    const ULONGLONG deadline = GetTickCount64() + waitMs;
    std::vector<BYTE> report;
    for (ULONGLONG now = GetTickCount64(); now < deadline; now = GetTickCount64()) {
        const int size = d.Read(report, (int)(deadline - now));
        if (size < 0) break;
        if (size < 13) continue;
        if (report[0] != 0x30 && !(acceptReply && report[0] == 0x21)) continue;
        const int level = (report[2] & 0xE0) >> 5;
        if (level > 4) continue;
        status = report[2];
        return level;
    }
    return -1;
}

}  // namespace

void PollSwitchPro(PollContext& ctx) {
    constexpr USHORT vid = 0x057E;
    constexpr USHORT pid = 0x2009;
    static constexpr const wchar_t* levels[] = {
        L"Empty", L"Critical", L"Low", L"Medium", L"Full",
    };

    for (const HidColl* c : ctx.Find(vid, pid, [](const HidColl& c) {
             return c.page() == 0x01 && (c.usage() == 0x04 || c.usage() == 0x05) && c.inLen() >= 13;
         })) {
        int level = -1;
        BYTE status = 0;
        {
            HidDev d;
            if (c->outLen() >= 11 && d.Open(*c)) {
                // If Steam or another host already streams full 0x30 reports (60+ per second), read
                // those and send nothing, so its own subcommand replies are left alone.
                d.Flush();
                level = ReadLevel(d, status, false, 100);
                if (level < 0 && d.Write(DeviceInfoRequest(), 200)) level = ReadLevel(d, status, true);
            }
        }
        if (level < 0) {
            HidDev d;
            if (!d.OpenReadOnly(*c)) continue;
            level = ReadLevel(d, status, false);
        }
        if (level < 0) continue;

        // Use the HID path for identity, as in the PlayStation driver.
        UINT pathHash = 2166136261u;
        for (wchar_t ch : c->path) {
            pathHash ^= ch;
            pathHash *= 16777619u;
        }
        DeviceStatus s;
        s.key = L"switchpro:" + std::to_wstring(pathHash);
        s.name = HidProduct(*c);
        // Over Bluetooth the pad names itself "Wireless Gamepad", which says nothing.
        if (s.name.empty() || s.name == L"Wireless Gamepad") s.name = L"Nintendo Switch Pro Controller";
        s.kind = DeviceKind::Gamepad;
        s.via = L"Switch mode";
        s.percent = 25 * level;
        s.levelText = levels[level];
        s.charging = (status & 0x10) != 0 && level < 4;
        s.graceSec = 300;
        ctx.Add(s, vid, pid);
    }
}
