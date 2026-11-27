// ROCCAT Syn Max Air headset on its 2.4 GHz receiver (1E7D:3A3F), a Turtle Beach "Stellar Wireless"
// platform. The receiver relays requests on its vendor collection (FF00, 64-byte unnumbered reports):
//   request 8A 07 06 01
//   reply   8A 83 06 02 <level 0..5, 6 charging> 00 <mV lo> <mV hi> <filtered mV, 2 bytes> <01 = charging>
// Byte 0 is a tag the headset echoes; the receiver answers other tags itself and the headset pushes
// E0..E6 events, both skipped. The percentage comes from the voltage (curve measured under audio + RGB
// load over a 16 h discharge); while charging the voltage means little and only the charging state is shown.
// From USBPcap captures of ROCCAT Swarm for ferloom/Wireless-Device-Battery-Monitor; untested here.
#include "drivers.h"

namespace {

const USHORT kVid = 0x1E7D;
const USHORT kSynMaxAir = 0x3A3F;

const int kMv[] = { 4080, 4040, 4000, 3960, 3920, 3900, 3880, 3840, 3820, 3800, 3780, 3760, 3740, 3720, 3700, 3680, 3660, 3640, 3600, 3500 };
const int kPct[] = { 100, 97, 92, 86, 78, 74, 70, 66, 63, 60, 53, 45, 34, 26, 20, 15, 11, 7, 3, 0 };

int PercentFromMv(int mv) {
    const int n = (int)(sizeof(kMv) / sizeof(kMv[0]));
    if (mv >= kMv[0]) return kPct[0];
    if (mv <= kMv[n - 1]) return kPct[n - 1];
    for (int i = 0; i + 1 < n; i++)
        if (mv <= kMv[i] && mv >= kMv[i + 1])
            return kPct[i + 1] + (mv - kMv[i + 1]) * (kPct[i] - kPct[i + 1]) / (kMv[i] - kMv[i + 1]);
    return 0;
}

bool Query(const HidColl& c, std::vector<BYTE>& r) {
    HidDev d;
    if (!d.Open(c)) return false;
    d.Flush();
    if (!d.Write({ 0x00, 0x8A, 0x07, 0x06, 0x01 })) return false;
    ULONGLONG end = GetTickCount64() + 1500;
    while ((LONGLONG)end - (LONGLONG)GetTickCount64() > 0) {
        int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
        if (d.Read(r, left) < 0) return false;
        if (r.size() >= 10 && r[0] == 0x8A && r[1] == 0x83 && r[2] == 0x06) return true;
    }
    return false;
}

}  // namespace

void PollRoccat(PollContext& ctx) {
    if (ctx.IsClaimed(kVid, kSynMaxAir)) return;
    auto colls = ctx.Find(kVid, kSynMaxAir, [](const HidColl& c) { return c.page() == 0xFF00 && c.outLen() >= 64; });
    if (colls.empty()) return;
    DeviceStatus s;
    s.key = L"roccat:" + Hex4(kSynMaxAir);
    s.name = L"ROCCAT Syn Max Air";
    s.kind = DeviceKind::Headset;
    s.via = L"2.4 GHz receiver";
    s.graceSec = 60;
    std::vector<BYTE> r;
    bool ok = false;
    for (const HidColl* c : colls)
        if ((ok = Query(*c, r))) break;
    if (!ok) {
        s.connected = false;  // receiver present, headset off or asleep
    } else {
        const int mv = r[6] | (r[7] << 8);
        s.millivolts = mv;
        s.charging = r.size() > 10 && r[10] == 0x01;
        if (s.charging) {
            s.full = mv >= 4180;
            if (s.full) s.levelText = L"Full";
        } else {
            s.percent = PercentFromMv(mv);
        }
    }
    ctx.Add(s, kVid, kSynMaxAir);
}
