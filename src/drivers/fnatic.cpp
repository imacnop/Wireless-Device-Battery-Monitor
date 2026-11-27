// FNATIC BOLT mouse on its 2.4 GHz receiver (2F0E:0203) or USB cable (2F0E:0204).
// The receiver's vendor collection (interface 1, 00FF:0001, 65-byte reports, report id 0) answers a
// read-only 0C query: reply 0C <percent>. The cable answers the same query with a stuck 100%, so it is
// never asked: with the cable in, the level still comes from the receiver and charging is inferred.
// From USBPcap captures of FNATIC OP for ferloom/Wireless-Device-Battery-Monitor (PR #48, tested on
// hardware there); untested here.
#include "drivers.h"

namespace {

const USHORT kVid = 0x2F0E;
const USHORT kReceiver = 0x0203;
const USHORT kCable = 0x0204;

int QueryReceiver(const HidColl& c) {
    HidDev d;
    if (!d.Open(c)) return -1;
    d.Flush();
    if (!d.Write({ 0x00, 0x0C })) return -1;
    ULONGLONG end = GetTickCount64() + 1200;
    std::vector<BYTE> r;
    while ((LONGLONG)end - (LONGLONG)GetTickCount64() > 0) {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (d.Read(r, left) < 0) return -1;
        if (r.size() == 64 && r[0] == 0x0C && r[1] <= 100) return r[1];
    }
    return -1;
}

}  // namespace

void PollFnatic(PollContext& ctx) {
    const bool cable = !ctx.Find(kVid, kCable, [](const HidColl&) { return true; }).empty();
    const bool dongle = !ctx.Find(kVid, kReceiver, [](const HidColl&) { return true; }).empty();
    if (!cable && !dongle) return;
    if (ctx.IsClaimed(kVid, kCable) || ctx.IsClaimed(kVid, kReceiver)) return;

    int pct = -1;
    for (const HidColl* c : ctx.Find(kVid, kReceiver, [](const HidColl& c) {
             return c.iface == 1 && c.page() == 0x00FF && c.usage() == 0x0001 && c.inLen() == 65 && c.outLen() == 65;
         }))
        if ((pct = QueryReceiver(*c)) >= 0) break;

    DeviceStatus s;
    s.key = L"fnatic:bolt";
    s.name = L"FNATIC BOLT";
    s.kind = DeviceKind::Mouse;
    s.via = cable ? L"USB cable" : L"2.4 GHz receiver";
    s.percent = pct;
    s.charging = cable && pct != 100;
    s.graceSec = 900;  // the receiver can miss a read; keep its last level for a while
    if (!cable && pct < 0) s.connected = false;
    ctx.Add(s, kVid, cable ? kCable : kReceiver);
    ctx.Claim(kVid, kCable);
    ctx.Claim(kVid, kReceiver);
}
