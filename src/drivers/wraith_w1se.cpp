// Wraith W1 SE mouse on its 2.4 GHz receiver 0738:A0AF (cable 0738:A09F).
// Protocol from the Wraith web driver (www.w1.software, the "w1se" module), as ported by BatteryDock
// (noctablabs/BatteryDock, GPL-3.0): vendor collection FF00:FF00, report id 0, 32 data bytes.
//   Status query: 0A AA FF DD 00 ... <checksum>, checksum = 255 - (sum of bytes 0..30 & FF).
//   Reply: 0A AA FF DD <battery %> <flags> <dpi stage> <polling> <dpi count> ...
//   flags bit 6 = charging; battery 0 while not charging = mouse asleep or off.
// Only the status query the web driver itself sends every 5 s is used; the DPI watcher reads the active
// level from the same reply (BatteryDock also reads the level values in a settings session; not done here).
#include <algorithm>
#include <cstdint>
#include <memory>
#include <mutex>

#include "drivers.h"
#include "dpi_source.h"

namespace {

const USHORT kVid = 0x0738;
const USHORT kReceiver = 0xA0AF;
const USHORT kCable = 0xA09F;

bool IsStatus(const std::vector<BYTE>& r, size_t o) {
    return r.size() >= o + 7 && r[o] == 0x0A && r[o + 1] == 0xAA && r[o + 2] == 0xFF && r[o + 3] == 0xDD;
}

struct Status { int percent = -1, stage = -1, count = 0; bool charging = false, asleep = false; };

bool Parse(const std::vector<BYTE>& r, Status& out) {
    size_t o = IsStatus(r, 1) ? 1 : IsStatus(r, 0) ? 0 : SIZE_MAX;  // with or without the report id byte
    if (o == SIZE_MAX) return false;
    out.charging = (r[o + 5] & 0x40) != 0;
    out.percent = std::min<int>(100, r[o + 4]);
    out.asleep = out.percent == 0 && !out.charging;
    if (r.size() >= o + 9) {
        int stage = r[o + 6], count = r[o + 8];
        if (count >= 1 && count <= 8 && stage < count) { out.stage = stage; out.count = count; }
    }
    return true;
}

// The battery poll and the DPI watcher talk to the same mouse: one exchange at a time.
std::mutex g_io;

bool Query(HidDev& d, const HidColl& c, Status& out, int timeoutMs = 800, int attempts = 2) {
    std::lock_guard<std::mutex> lock(g_io);
    std::vector<BYTE> req(c.outLen(), 0);
    req[1] = 0x0A; req[2] = 0xAA; req[3] = 0xFF; req[4] = 0xDD;
    unsigned sum = 0;
    for (int i = 1; i <= 31; i++) sum += req[i];
    req[32] = (BYTE)(255 - (sum & 0xFF));
    for (int attempt = 0; attempt < attempts; attempt++) {
        d.Flush();
        if (!d.Write(req)) return false;
        ULONGLONG end = GetTickCount64() + (ULONGLONG)timeoutMs;
        std::vector<BYTE> r;
        for (;;) {
            int left = (int)((LONGLONG)end - (LONGLONG)GetTickCount64());
            if (left <= 0 || d.Read(r, left) < 0) break;
            if (Parse(r, out)) return true;
        }
    }
    return false;
}

bool IsStatusCollection(const HidColl& c) {
    return c.vid == kVid && (c.pid == kReceiver || c.pid == kCable) && c.page() == 0xFF00 && c.outLen() >= 33 && c.inLen() >= 33;
}

class WraithDpi : public DpiSource {
public:
    explicit WraithDpi(const HidColl& c) : c_(c) {}
    bool Open() { return d_.Open(c_); }
    std::wstring Key() const override { return L"wraithw1se"; }
    USHORT Vid() const override { return kVid; }
    unsigned IntervalMs() const override { return 250; }  // the receiver answers in a few ms
    bool Read(DpiReading& out) override {
        Status st;
        if (!Query(d_, c_, st, 400, 1) || st.asleep || st.stage < 0) return false;
        out.stage = st.stage;
        out.count = st.count;
        return true;
    }
private:
    HidColl c_;
    HidDev d_;
};

}  // namespace

void FindWraithDpi(const std::vector<HidColl>& colls, DpiSources& out) {
    for (const HidColl& c : colls) {
        if (!IsStatusCollection(c)) continue;
        auto s = std::make_unique<WraithDpi>(c);
        if (s->Open()) { out.push_back(std::move(s)); return; }  // one mouse: receiver or cable
    }
}

void PollWraithW1SE(PollContext& ctx) {
    for (USHORT pid : { kReceiver, kCable }) {
        if (ctx.IsClaimed(kVid, pid)) continue;
        for (const HidColl* c : ctx.Find(kVid, pid, [](const HidColl& c) {
                 return c.page() == 0xFF00 && c.outLen() >= 33 && c.inLen() >= 33;
             })) {
            HidDev d;
            if (!d.Open(*c)) continue;
            Status st;
            if (!Query(d, *c, st)) continue;
            DeviceStatus s;
            s.key = L"wraithw1se";
            s.name = L"Wraith W1 SE";
            s.kind = DeviceKind::Mouse;
            s.via = pid == kCable ? L"USB cable" : L"2.4 GHz receiver";
            s.graceSec = 1800;
            s.charging = st.charging;
            if (st.asleep) s.asleep = true;
            else s.percent = st.percent;
            ctx.Add(s, kVid, pid);
            ctx.Claim(kVid, kReceiver);  // receiver and cable are one mouse
            ctx.Claim(kVid, kCable);
            break;
        }
    }
}
