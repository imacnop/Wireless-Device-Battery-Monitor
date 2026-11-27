#pragma once
// Mice that can tell their current DPI. A brand driver that knows how to read it offers a DpiSource; the
// DPI watcher (src/dpi_watch.cpp) asks it a few times a second and shows a bubble when the value changes.
// Every query only reads; nothing writes a mouse setting. Design from BatteryDock (noctablabs/BatteryDock).
#include <memory>
#include <string>
#include <vector>

#include "../hid.h"

struct DpiReading {
    int dpi = 0;     // current DPI, 0 when the mouse reports only a level
    int stage = -1;  // 0-based active level, -1 when unknown
    int count = 0;   // number of levels, 0 when unknown
};

class DpiSource {
public:
    virtual ~DpiSource() = default;
    // Device key as the battery driver reports it (DeviceStatus::key), so the bubble can point at it.
    virtual std::wstring Key() const = 0;
    virtual USHORT Vid() const = 0;
    // One reading; false when the mouse did not answer (asleep, out of range, or busy).
    virtual bool Read(DpiReading& out) = 0;
    // How often to read while the mouse answers.
    virtual unsigned IntervalMs() const { return 400; }
};

using DpiSources = std::vector<std::unique_ptr<DpiSource>>;

// Brand finders (each opens its own handles). `colls` is a fresh EnumHid() list.
void FindLogitechDpi(const std::vector<HidColl>& colls, DpiSources& out);
void FindRazerDpi(const std::vector<HidColl>& colls, DpiSources& out);
void FindWraithDpi(const std::vector<HidColl>& colls, DpiSources& out);

inline DpiSources FindDpiSources(const std::vector<HidColl>& colls) {
    DpiSources out;
    FindWraithDpi(colls, out);
    FindLogitechDpi(colls, out);
    FindRazerDpi(colls, out);
    return out;
}
