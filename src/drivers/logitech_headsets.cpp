// Logitech LIGHTSPEED headsets whose receivers have no classic HID++ collections.
//
// G321 (046D:0B39): interface 3, vendor collection FF00:0212 (67-byte feature, 61-byte input), relays to
// the headset: feature report 24 2B 00, input reply 27 2B 00 0B <6-byte address> <4-byte version> <percent>.
// No charging flag. From G321 Studio (LightspeedConnection.cs, MIT); tested on the headset for
// ferloom/Wireless-Device-Battery-Monitor (PR #107).
//
// G735 (046D:0AD8): vendor collection FF03:0001, report F2, 271 bytes each way, wrapping HID++ 2.0 after
// a 16-byte header. Root getFeature(0104) gives the battery feature's index; its function 0 answers
// <level %> <next level> <state>, state 1 / 2 = charging. Software id 0B keeps replies apart from G HUB's.
// From TaskbarVitals (logitech_g735.cpp, a G HUB capture reproduced on the headset); untested here.
#include "drivers.h"

namespace {

const USHORT kVid = 0x046D;

void PollG321(PollContext& ctx) {
    const USHORT pid = 0x0B39;
    if (ctx.IsClaimed(kVid, pid)) return;
    auto colls = ctx.Find(kVid, pid, [](const HidColl& c) {
        return c.iface == 3 && c.page() == 0xFF00 && c.usage() == 0x0212 && c.featLen() >= 3 && c.inLen() > 14;
    });
    if (colls.empty()) return;
    DeviceStatus s;
    s.key = L"logi-g321:" + Hex4(pid);
    s.name = L"Logitech G321 LIGHTSPEED";
    s.kind = DeviceKind::Headset;
    s.via = L"2.4 GHz receiver";
    s.graceSec = 60;
    bool ok = false;
    for (const HidColl* c : colls) {
        HidDev d;
        if (!d.Open(*c)) continue;
        d.Flush();
        if (!d.SetFeature({ 0x24, 0x2B, 0x00 })) continue;
        ULONGLONG end = GetTickCount64() + 800;
        std::vector<BYTE> r;
        while (!ok && (LONGLONG)end - (LONGLONG)GetTickCount64() > 0) {
            int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
            if (d.Read(r, left) < 0) break;
            if (r.size() > 14 && r[0] == 0x27 && r[1] == 0x2B && r[2] == 0x00 && r[3] == 0x0B && r[14] <= 100) {
                s.percent = r[14];
                ok = true;
            }
        }
        if (ok) break;
    }
    if (!ok) s.connected = false;  // receiver present, headset off or out of range
    ctx.Add(s, kVid, pid);
}

const BYTE kG735Header[16] = { 0xF2, 0x01, 0x05, 0x1E, 0xFA, 0x82, 0x40, 0xCE, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00 };
const BYTE kSw = 0x0B;

// Sends (index, function, params) and returns the reply data after index and function/sw.
bool G735Call(HidDev& d, ULONGLONG end, BYTE index, BYTE function, std::initializer_list<BYTE> params, std::vector<BYTE>& data) {
    std::vector<BYTE> req(kG735Header, kG735Header + 16);
    req.push_back(index);
    req.push_back((BYTE)((function << 4) | kSw));
    req.insert(req.end(), params.begin(), params.end());
    d.Flush();
    if (!d.Write(req, 250)) return false;
    const BYTE fnsw = (BYTE)((function << 4) | kSw);
    std::vector<BYTE> r;
    while ((LONGLONG)end - (LONGLONG)GetTickCount64() > 0) {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (d.Read(r, left) < 0) return false;
        if (r.size() < 20 || r[0] != 0xF2 || r[1] != 0x00) continue;  // byte 1 = 01 is our own request
        const BYTE* m = r.data() + 16;
        if (m[0] == index && m[1] == fnsw) { data.assign(r.begin() + 18, r.end()); return true; }
        if (m[0] == 0xFF && m[1] == index && m[2] == fnsw) return false;  // HID++ error
    }
    return false;
}

void PollG735(PollContext& ctx) {
    const USHORT pid = 0x0AD8;
    if (ctx.IsClaimed(kVid, pid)) return;
    auto colls = ctx.Find(kVid, pid, [](const HidColl& c) {
        return c.page() == 0xFF03 && c.usage() == 0x0001 && c.inLen() == 271 && c.outLen() == 271;
    });
    if (colls.empty()) return;
    DeviceStatus s;
    s.key = L"g735:" + Hex4(pid);
    s.name = L"Logitech G735";
    s.kind = DeviceKind::Headset;
    s.via = L"2.4 GHz receiver";
    s.graceSec = 60;
    bool ok = false;
    HidDev d;
    if (d.Open(*colls[0])) {
        ULONGLONG end = GetTickCount64() + 1000;
        std::vector<BYTE> data;
        if (G735Call(d, end, 0x00, 0, { 0x01, 0x04 }, data) && !data.empty() && data[0] != 0) {
            const BYTE index = data[0];
            if (G735Call(d, end, index, 0, {}, data) && !data.empty() && data[0] <= 100) {
                s.percent = data[0];
                const BYTE state = data.size() > 2 ? data[2] : 0;
                s.charging = state == 1 || state == 2;
                ok = true;
            }
        }
    }
    if (!ok) s.connected = false;
    ctx.Add(s, kVid, pid);
}

}  // namespace

void PollLogitechHeadsets(PollContext& ctx) {
    PollG321(ctx);
    PollG735(ctx);
}
