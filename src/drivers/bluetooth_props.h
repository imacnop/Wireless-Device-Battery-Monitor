#pragma once
// Windows device properties of Bluetooth devices, shared by the fork's Bluetooth drivers (phones.cpp).
// generic.cpp keeps its own copies on purpose: it is an upstream file, and touching it would conflict
// with every upstream change to it. New code should use these instead of copying them again.
#include <windows.h>
#include <setupapi.h>
#include <devpropdef.h>

#include <cwchar>
#include <string>

namespace btprops {

// Battery level Windows keeps for a device (Settings > Bluetooth & devices), one byte 0-100.
inline const DEVPROPKEY kBattery = { { 0x104EA319, 0x6EE2, 0x4701, { 0xBD, 0x47, 0x8D, 0xDB, 0xF4, 0x25, 0xBB, 0xE5 } }, 2 };
// Whether the device node is connected right now (boolean).
inline const DEVPROPKEY kIsConnected = { { 0x83DA6326, 0x97A6, 0x4088, { 0x94, 0x53, 0xA1, 0x92, 0x3F, 0x57, 0x3B, 0x29 } }, 15 };
// Classic Bluetooth Class of Device (UINT32).
inline const DEVPROPKEY kClassOfDevice = { { 0x2BD67D8B, 0x8BEB, 0x48D5, { 0x87, 0xE0, 0x6C, 0xDA, 0x34, 0x28, 0x04, 0x0A } }, 10 };

// Bluetooth address from a main device node id ("BTHENUM\DEV_6CFBED129847\..." or "BTHLE\DEV_..."), 0 if none.
inline UINT64 Address(const wchar_t* inst) {
    const wchar_t* p = wcsstr(inst, L"\\DEV_");
    if (!p) return 0;
    p += 5;
    UINT64 a = 0;
    for (int i = 0; i < 12; i++) {
        wchar_t c = p[i];
        int v = c >= L'0' && c <= L'9' ? c - L'0' : c >= L'A' && c <= L'F' ? c - L'A' + 10 : c >= L'a' && c <= L'f' ? c - L'a' + 10 : -1;
        if (v < 0) return 0;
        a = a << 4 | (UINT64)v;
    }
    return a;
}

// A string property, or the first entry of a string list (container category), "" if none.
inline std::wstring String(HDEVINFO di, SP_DEVINFO_DATA* dd, const DEVPROPKEY& key) {
    DEVPROPTYPE t;
    wchar_t buf[256] = {};
    if (SetupDiGetDevicePropertyW(di, dd, &key, &t, (PBYTE)buf, sizeof(buf) - sizeof(wchar_t), nullptr, 0) &&
        (t == DEVPROP_TYPE_STRING || t == DEVPROP_TYPE_STRING_LIST))
        return buf;
    return L"";
}

// Service node names carry the profile: "Pixel 8 Hands-Free AG" -> "Pixel 8".
inline std::wstring StripServiceSuffix(std::wstring n) {
    for (const wchar_t* suf : { L" Hands-Free AG", L" Hands-Free", L" Avrcp Transport", L" Stereo", L" Audio Gateway" }) {
        size_t l = wcslen(suf);
        if (n.size() > l && n.compare(n.size() - l, l, suf) == 0) return n.substr(0, n.size() - l);
    }
    return n;
}

}  // namespace btprops
