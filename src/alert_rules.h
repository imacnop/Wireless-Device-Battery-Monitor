#pragma once
// Pure, portable policy for per-device low-battery notifications.
// No HID access, Windows dependencies, timers or disk/network writes.
#include "devices.h"

#include <map>
#include <string>

namespace alerts {
constexpr int kDefaultThreshold = 15;
constexpr int kUseDefault = -2;   // no per-device override persisted
constexpr int kDisabled = -1;     // notifications disabled for this device
constexpr int kThresholds[] = {5, 10, 15, 20, 25, 30, 35, 40};
constexpr int kThresholdCount = sizeof(kThresholds) / sizeof(kThresholds[0]);

inline bool ValidOverride(int value) {
    if (value == kDisabled) return true;
    for (int threshold : kThresholds)
        if (value == threshold) return true;
    return false;
}

inline int OverrideFor(const std::map<std::wstring, int>& overrides, const std::wstring& key) {
    const auto it = overrides.find(key);
    return it == overrides.end() ? kUseDefault : it->second;
}

inline int EffectiveThreshold(const std::map<std::wstring, int>& overrides, const std::wstring& key) {
    int value = OverrideFor(overrides, key);
    return value == kUseDefault ? kDefaultThreshold : value;
}

// This is invoked only for a fresh poll result. Unknown, sleeping and disconnected
// readings don't reset the latch: a transient missing report must not spam alerts.
inline bool ShouldNotify(const DeviceStatus& s, int threshold, bool& warned) {
    if (threshold == kDisabled) { warned = false; return false; }
    if (!s.connected || s.asleep || s.percent < 0 || s.percent > 100) return false;
    if (s.charging) { warned = false; return false; }
    if (s.percent > threshold + 10) { warned = false; return false; }
    if (s.percent <= threshold && !warned) {
        warned = true;
        return true;
    }
    return false;
}

// "Unplug to save the battery": charging past `limit` (e.g. 80%) wears a lithium battery faster, so the
// app can say when the level crosses it. Only when it is seen crossing (previous reading below the limit),
// so starting the app on a device already at 90% says nothing; once per charge, re-armed when unplugged
// or after the level drops 5 points below the limit. A jump straight to 100% is left to "fully charged".
constexpr int kChargeLimits[] = {80, 85, 90};
constexpr int kChargeLimitCount = sizeof(kChargeLimits) / sizeof(kChargeLimits[0]);

inline bool ShouldNotifyChargeLimit(const DeviceStatus& s, int limit, int previousPercent, bool& warned) {
    if (limit <= 0) { warned = false; return false; }
    if (!s.connected || s.asleep || s.percent < 0 || s.percent > 100) return false;
    if (!s.charging || s.percent < limit - 5) { warned = false; return false; }
    if (!warned && s.percent >= limit && s.percent < 100 && previousPercent >= 0 && previousPercent < limit) {
        warned = true;
        return true;
    }
    return false;
}

// "Fully charged": once when a charging device reaches 100% (or says it is full), only when seen getting
// there, so a device already full when the app starts says nothing. Re-armed once it is off power.
inline bool ShouldNotifyFull(const DeviceStatus& s, int previousPercent, bool& warned) {
    if (!s.connected || s.asleep) return false;
    const bool full = s.full || (s.charging && s.percent >= 100);
    if (!s.charging && !s.full) { warned = false; return false; }
    if (full && !warned && previousPercent >= 0 && previousPercent < 100) {
        warned = true;
        return true;
    }
    if (full) warned = true;
    return false;
}

// Quiet hours: no notifications from `startHour` until `endHour` (local time, may wrap past midnight).
inline bool InQuietHours(int hour, int startHour, int endHour) {
    if (startHour == endHour) return false;
    return startHour < endHour ? hour >= startHour && hour < endHour : hour >= startHour || hour < endHour;
}
}  // namespace alerts
