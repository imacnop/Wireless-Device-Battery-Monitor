#pragma once
#include <cwchar>
#include <cwctype>
#include <map>
#include <string>
#include <vector>
#include <utility>
namespace PhoneLink {
struct Reading { std::wstring name; int percent=-1; bool charging=false; };
namespace Detail {
struct Value {
    wchar_t kind=0;
    std::wstring text;
    std::map<std::wstring, Value> object;
    std::vector<Value> array;
};
// The card is an undocumented format. Bound nesting and size, reject malformed JSON,
// and inspect fields on their owning object rather than searching notification text.
class Parser {
    const std::wstring& input;
    size_t at=0, nodes=0;
    void Space(){while(at<input.size() && (input[at]==L' '||input[at]==L'\r'||input[at]==L'\n'||input[at]==L'\t'))++at;}
    bool Take(wchar_t c){Space();if(at==input.size()||input[at]!=c)return false;++at;return true;}
    bool String(std::wstring& out){
        if(!Take(L'"'))return false;
        while(at<input.size()){
            wchar_t c=input[at++];if(c==L'"')return true;if(c<32)return false;
            if(c!=L'\\'){out+=c;continue;}
            if(at==input.size())return false;
            c=input[at++];
            if(c==L'u'){
                unsigned v=0;
                for(int i=0;i<4;++i){if(at==input.size())return false;wchar_t h=input[at++];unsigned n;
                    if(h>=L'0'&&h<=L'9')n=h-L'0';else if(h>=L'a'&&h<=L'f')n=h-L'a'+10;else if(h>=L'A'&&h<=L'F')n=h-L'A'+10;else return false;v=v*16+n;}
                out+=static_cast<wchar_t>(v);
            }else if(c==L'"'||c==L'\\'||c==L'/')out+=c;
            else if(c==L'n')out+=L'\n';else if(c==L't')out+=L'\t';else if(c==L'r')out+=L'\r';else if(c==L'b')out+=L'\b';else if(c==L'f')out+=L'\f';else return false;
        }return false;
    }
    bool Read(Value& v,int depth){
        Space();if(depth>64||++nodes>20000||at==input.size())return false;
        wchar_t c=input[at];v.kind=c;
        if(c==L'"')return String(v.text);
        if(c==L'{'){
            ++at;if(Take(L'}'))return true;
            do{std::wstring key;Value child;if(!String(key)||!Take(L':')||!Read(child,depth+1)||v.object.count(key))return false;v.object.emplace(std::move(key),std::move(child));if(Take(L'}'))return true;}while(Take(L','));return false;
        }
        if(c==L'['){++at;if(Take(L']'))return true;do{Value child;if(!Read(child,depth+1))return false;v.array.push_back(std::move(child));if(Take(L']'))return true;}while(Take(L','));return false;}
        for(const wchar_t* literal:{L"true",L"false",L"null"}){size_t n=wcslen(literal);if(input.compare(at,n,literal)==0){at+=n;return true;}}
        if(c==L'-')++at;
        if(at==input.size())return false;
        if(input[at]==L'0')++at;else {if(input[at]<L'1'||input[at]>L'9')return false;while(at<input.size()&&input[at]>=L'0'&&input[at]<=L'9')++at;}
        if(at<input.size()&&input[at]==L'.'){++at;size_t start=at;while(at<input.size()&&input[at]>=L'0'&&input[at]<=L'9')++at;if(at==start)return false;}
        if(at<input.size()&&(input[at]==L'e'||input[at]==L'E')){++at;if(at<input.size()&&(input[at]==L'+'||input[at]==L'-'))++at;size_t start=at;while(at<input.size()&&input[at]>=L'0'&&input[at]<=L'9')++at;if(at==start)return false;}
        return true;
    }
public:
    explicit Parser(const std::wstring& s):input(s){}
    bool Parse(Value& v){if(input.size()>4*1024*1024||!Read(v,0))return false;Space();return at==input.size();}
};
inline const std::wstring* Field(const Value& v,const wchar_t* key){auto i=v.object.find(key);return i!=v.object.end()&&i->second.kind==L'"'?&i->second.text:nullptr;}
} // namespace Detail

// The card follows the Windows display language. Each locale gives the "connected" suffix of the
// card's "speak" field and the battery tooltip: "<prefix><n>%<remaining>[, <state>...]".
//  * English: "Pixel 8 - Connected to your PC", "Battery 73% remaining, charging".
//  * French (Mobile connecté), from a real card: "S26 de ... - Connecté à votre PC",
//    "Batterie 56 % restante, économie d'énergie" (a space before "%").
//  * Japanese (issue #2738): "iPhone - PC に接続しました", "バッテリー 残量 55%".
struct Locale {
    const wchar_t* connected;  // ending of "speak" while the phone is connected
    const wchar_t* prefix;     // battery tooltip start
    const wchar_t* remaining;  // right after "%" ("" = nothing may follow)
    const wchar_t* charging;   // a ", "-separated state containing this means charging (null = unknown)
};
inline const Locale* Locales(size_t& count) {
    static const Locale k[] = {
        { L" - Connected to your PC", L"Battery ", L" remaining", L"charging" },
        { L" - PC に接続しました", L"バッテリー 残量 ", L"", nullptr },
        { L" - Connecté à votre PC", L"Batterie ", L" restante", L"en charge" },
    };
    count = sizeof(k) / sizeof(k[0]);
    return k;
}

// Space allowed between the number and "%" (French typography): space, no-break, narrow no-break.
inline bool PercentSpace(wchar_t c) { return c == L' ' || c == 0x00A0 || c == 0x202F; }

// Locale index whose connected suffix ends `speak` (name = the part before it), or -1.
inline int ConnectedLocale(const std::wstring& speak, std::wstring& name) {
    size_t count = 0;
    const Locale* k = Locales(count);
    for (size_t i = 0; i < count; ++i) {
        const std::wstring ending(k[i].connected);
        if (speak.size() > ending.size() && speak.compare(speak.size() - ending.size(), ending.size(), ending) == 0) {
            name = speak.substr(0, speak.size() - ending.size());
            return name.empty() ? -1 : (int)i;
        }
    }
    return -1;
}

inline bool ConnectedPhoneName(const std::wstring& speak, std::wstring& name) { return ConnectedLocale(speak, name) >= 0; }

// Battery level from a tooltip in one locale, or -1.
inline int BatteryInLocale(const std::wstring& tooltip, const Locale& l, bool& charging) {
    const std::wstring prefix(l.prefix);
    if (tooltip.compare(0, prefix.size(), prefix) != 0) return -1;
    size_t start = prefix.size(), end = start;
    while (end < tooltip.size() && tooltip[end] >= L'0' && tooltip[end] <= L'9' && end - start < 3) ++end;
    if (end == start) return -1;
    size_t pct = end;
    if (pct < tooltip.size() && PercentSpace(tooltip[pct])) ++pct;
    if (pct >= tooltip.size() || tooltip[pct] != L'%') return -1;
    int level = std::stoi(tooltip.substr(start, end - start));
    if (level > 100) return -1;

    const std::wstring rest = tooltip.substr(pct + 1), remaining(l.remaining);
    charging = false;
    if (remaining.empty()) return rest.empty() ? level : -1;
    if (rest.compare(0, remaining.size(), remaining) != 0) return -1;
    std::wstring states = rest.substr(remaining.size());
    if (states.empty()) return level;
    if (states.compare(0, 2, L", ") != 0) return -1;
    // ", charging", ", battery saver", ", économie d'énergie", ...
    for (size_t at = 2; at <= states.size();) {
        size_t comma = states.find(L", ", at);
        std::wstring state = states.substr(at, comma == std::wstring::npos ? std::wstring::npos : comma - at);
        if (l.charging && state.find(l.charging) != std::wstring::npos) charging = true;
        if (comma == std::wstring::npos) break;
        at = comma + 2;
    }
    return level;
}

inline int BatteryFromTooltip(const std::wstring& tooltip, bool& charging) {
    size_t count = 0;
    const Locale* k = Locales(count);
    for (size_t i = 0; i < count; ++i) {
        int n = BatteryInLocale(tooltip, k[i], charging);
        if (n >= 0) return n;
    }
    return -1;
}

namespace Detail {
// The visible text next to the icon: "56%" or "56 %".
inline bool PercentText(const std::wstring& text, int n) {
    const std::wstring num = std::to_wstring(n);
    if (text.compare(0, num.size(), num) != 0) return false;
    size_t at = num.size();
    if (at < text.size() && PercentSpace(text[at])) ++at;
    return at + 1 == text.size() && text[at] == L'%';
}
inline void Battery(const Value& value,const Locale& locale,int& percent,bool& charging,bool& ambiguous){
    if(value.kind==L'['){for(const auto& child:value.array)Battery(child,locale,percent,charging,ambiguous);return;}
    if(value.kind!=L'{')return;
    const auto* type=Field(value,L"type");
    if(type && *type!=L"AdaptiveCard" && *type!=L"Container" && *type!=L"ColumnSet" && *type!=L"Column" && *type!=L"TextBlock" && *type!=L"Image")return;
    if(const auto* tooltip=Field(value,L"tooltip")){
        bool charge=false;
        int n=BatteryInLocale(*tooltip,locale,charge);  // only the language of the card's "speak"
        const auto* text=Field(value,L"text");
        if(n>=0 && (!text || PercentText(*text,n))){
            if(percent>=0 && (percent!=n||charging!=charge))ambiguous=true;
            percent=n;charging=charge;
        }
    }
    // Only traverse card layout fields, never actions, arbitrary metadata or notification payloads.
    for(const wchar_t* field:{L"body",L"items",L"columns"}){auto i=value.object.find(field);if(i!=value.object.end())Battery(i->second,locale,percent,charging,ambiguous);}
}

// First "<n>%" / "<n> %" in a tooltip, whatever the language around it, or -1.
inline int PercentInTooltip(const std::wstring& t){
    for(size_t i=0;i<t.size();++i){
        if(t[i]<L'0'||t[i]>L'9'||(i&&t[i-1]>=L'0'&&t[i-1]<=L'9'))continue;
        size_t e=i;while(e<t.size()&&t[e]>=L'0'&&t[e]<=L'9'&&e-i<3)++e;
        if(e<t.size()&&t[e]>=L'0'&&t[e]<=L'9')continue;  // 4+ digits
        size_t p=e;if(p<t.size()&&PercentSpace(t[p]))++p;
        if(p<t.size()&&t[p]==L'%'){int n=std::stoi(t.substr(i,e-i));return n<=100?n:-1;}
    }
    return -1;
}
// A TextBlock reading exactly "<n>%" in this layout element or the ones it holds.
inline bool HoldsPercentText(const Value& value,int n,int depth=0){
    if(depth>8)return false;
    if(value.kind==L'['){for(const auto& c:value.array)if(HoldsPercentText(c,n,depth+1))return true;return false;}
    if(value.kind!=L'{')return false;
    const auto* type=Field(value,L"type");
    if(type && *type==L"TextBlock"){const auto* text=Field(value,L"text");return text && PercentText(*text,n);}
    for(const wchar_t* field:{L"items",L"columns"}){auto i=value.object.find(field);if(i!=value.object.end()&&HoldsPercentText(i->second,n,depth+1))return true;}
    return false;
}
// Language-independent fallback for card languages not in Locales(): the battery block is a layout
// element whose tooltip holds "<n>%" and which shows a TextBlock reading exactly "<n>%" (as in the
// English, Japanese and French cards). Charging is unknown this way.
inline void StructuralBattery(const Value& value,int& percent,bool& ambiguous){
    if(value.kind==L'['){for(const auto& child:value.array)StructuralBattery(child,percent,ambiguous);return;}
    if(value.kind!=L'{')return;
    const auto* type=Field(value,L"type");
    if(type && *type!=L"AdaptiveCard" && *type!=L"Container" && *type!=L"ColumnSet" && *type!=L"Column" && *type!=L"TextBlock" && *type!=L"Image")return;
    if(const auto* tooltip=Field(value,L"tooltip")){
        int n=PercentInTooltip(*tooltip);
        if(n>=0 && HoldsPercentText(value,n)){
            if(percent>=0 && percent!=n)ambiguous=true;
            percent=n;
        }
    }
    for(const wchar_t* field:{L"body",L"items",L"columns"}){auto i=value.object.find(field);if(i!=value.object.end())StructuralBattery(i->second,percent,ambiguous);}
}
// A battery tooltip in a language of Locales(): then the card's "speak" not ending in that language's
// "connected" phrase means the phone is not connected, and the fallback must not be used.
inline bool KnownLanguageTooltip(const Value& value){
    if(value.kind==L'['){for(const auto& c:value.array)if(KnownLanguageTooltip(c))return true;return false;}
    if(value.kind!=L'{')return false;
    if(const auto* tooltip=Field(value,L"tooltip")){
        size_t count=0;const Locale* k=Locales(count);
        for(size_t i=0;i<count;++i)if(tooltip->rfind(k[i].prefix,0)==0)return true;
    }
    for(const wchar_t* field:{L"body",L"items",L"columns"}){auto i=value.object.find(field);if(i!=value.object.end()&&KnownLanguageTooltip(i->second))return true;}
    return false;
}
} // namespace Detail
inline bool Parse(const std::wstring& json,Reading& out){
    Detail::Value root;
    if(!Detail::Parser(json).Parse(root)||root.kind!=L'{')return false;
    const auto* speak=Detail::Field(root,L"speak");Reading parsed;
    if(!speak)return false;
    int locale=ConnectedLocale(*speak,parsed.name);
    bool ambiguous=false;
    if(locale<0){
        // Unknown card language: "<name> - <status>", battery from the layout alone (see StructuralBattery).
        size_t dash=speak->rfind(L" - ");
        if(dash==std::wstring::npos||dash==0||Detail::KnownLanguageTooltip(root))return false;
        parsed.name=speak->substr(0,dash);
        Detail::StructuralBattery(root,parsed.percent,ambiguous);
        if(ambiguous||parsed.percent<0)return false;
        out=std::move(parsed);return true;
    }
    size_t count=0;
    Detail::Battery(root,Locales(count)[locale],parsed.percent,parsed.charging,ambiguous);
    if(ambiguous||parsed.percent<0)return false;
    out=std::move(parsed);return true;
}
} // namespace PhoneLink
