
#include "../src/drivers/phonelink_protocol.h"

#include <cassert>
#include <cstdio>

int main() {
    PhoneLink::Reading reading;

    // Existing English card: battery and charging.
    const std::wstring english = LR"({
        "speak": "Pixel 8 - Connected to your PC",
        "body": [
            {"tooltip": "Open Phone Link"},
            {"text": "73%", "tooltip": "Battery 73% remaining, charging"}
        ]
    })";

    assert(PhoneLink::Parse(english, reading));
    assert(reading.name == L"Pixel 8");
    assert(reading.percent == 73);
    assert(reading.charging);

    // Japanese card from issue #2738.
    const std::wstring japanese = LR"({
        "speak": "iPhone - PC に接続しました",
        "body": [
            {
                "tooltip": "バッテリー 残量 55%",
                "altText": "現在、バッテリー 残量は  55%です",
                "text": "55%"
            }
        ]
    })";

    reading = {};
    assert(PhoneLink::Parse(japanese, reading));
    assert(reading.name == L"iPhone");
    assert(reading.percent == 55);
    assert(!reading.charging);

    // Reject disconnected/setup cards, even with a battery-like tooltip.
    const std::wstring disconnected = LR"({
        "speak": "Pixel 8 - Disconnected",
        "tooltip": "Battery 80% remaining"
    })";

    reading = {};
    assert(!PhoneLink::Parse(disconnected, reading));

    // Reject unrelated percentages and invalid battery levels.
    const std::wstring unrelated = LR"({
        "speak": "Pixel 8 - Connected to your PC",
        "tooltip": "Download progress 90%"
    })";
    assert(!PhoneLink::Parse(unrelated, reading));

    const std::wstring invalid = LR"({
        "speak": "Pixel 8 - Connected to your PC",
        "tooltip": "Battery 150% remaining"
    })";
    assert(!PhoneLink::Parse(invalid, reading));

    // Support whitespace around JSON separators.
    const std::wstring spaced = LR"({
        "speak" : "Phone - Connected to your PC",
        "tooltip" : "Battery 0% remaining",
        "text" : "0%"
    })";
    assert(PhoneLink::Parse(spaced, reading));
    assert(reading.percent == 0);
    assert(!reading.charging);

    // A Japanese disconnected phone must never be reported as connected.
    const std::wstring japaneseDisconnected = LR"({
        "speak": "iPhone - PC から切断されました",
        "tooltip": "バッテリー 残量 55%"
    })";

    reading = {};
    assert(!PhoneLink::Parse(japaneseDisconnected, reading));

    // Unknown localized connection messages must fail safely.
    const std::wstring unknownLocale = LR"({
        "speak": "iPhone - Connected",
        "tooltip": "Battery 55% remaining"
    })";

    assert(!PhoneLink::Parse(unknownLocale, reading));

    // An unrelated notification tooltip must not override the phone battery.
    const std::wstring unrelatedTooltip = LR"({
        "speak": "iPhone - PC に接続しました",
        "body": [
            {
                "type": "Notification",
                "tooltip": "Battery 99% remaining",
                "text": "Other device"
            },
            {
                "type": "TextBlock",
                "tooltip": "バッテリー 残量 55%",
                "text": "55%"
            }
        ]
    })";

    reading = {};
    assert(PhoneLink::Parse(unrelatedTooltip, reading));
    assert(reading.percent == 55);

    // Legacy English card with a battery tooltip but no text field.
    const std::wstring legacyEnglish = LR"({
        "speak": "Pixel 8 - Connected to your PC",
        "tooltip": "Battery 42% remaining"
    })";

    reading = {};
    assert(PhoneLink::Parse(legacyEnglish, reading));
    assert(reading.percent == 42);
    assert(!reading.charging);

    const std::wstring nestedLegacy = LR"({
        "speak": "Pixel 8 - Connected to your PC",
        "body": [
            {"tooltip": "Battery 45% remaining"}
        ]
    })";

    reading = {};
    assert(PhoneLink::Parse(nestedLegacy, reading));
    assert(reading.percent == 45);


    const std::wstring sameLanguage = LR"({"speak":"Phone - Connected to your PC","body":[{"type":"Notification","tooltip":"Battery 99% remaining"},{"type":"TextBlock","tooltip":"Battery 55% remaining","text":"55%"}]})";
    assert(PhoneLink::Parse(sameLanguage,reading) && reading.percent==55);
    const std::wstring ambiguous = LR"({"speak":"Phone - Connected to your PC","body":[{"tooltip":"Battery 99% remaining"},{"tooltip":"Battery 55% remaining"}]})";
    assert(!PhoneLink::Parse(ambiguous,reading));
    const std::wstring metadata = LR"({"speak":"Phone - Connected to your PC","metadata":{"tooltip":"Battery 99% remaining"},"body":[{"tooltip":"Battery 55% remaining"}]})";
    assert(PhoneLink::Parse(metadata,reading) && reading.percent==55);
    assert(!PhoneLink::Parse(LR"({"speak":"Phone - Connected to your PC","tooltip":"Battery 55% remaining","text":"Download complete"})",reading));
    assert(!PhoneLink::Parse(LR"({"speak":"Phone - Connected to your PC","tooltip":"Battery 55% remaining")",reading));
    assert(!PhoneLink::Parse(legacyEnglish+L"junk",reading));
    assert(!PhoneLink::Parse(LR"({"speak":"Phone - Connected to your PC","speak":"other","tooltip":"Battery 55% remaining"})",reading));
    assert(PhoneLink::Parse(LR"({"speak":"Phone - Connected to your PC","tooltip":"Battery \u0035\u0035% remaining"})",reading) && reading.percent==55);
    assert(!PhoneLink::Parse(LR"({"speak":"Phone - Connected to your PC","tooltip":"Battery \uZZZZ% remaining"})",reading));
    std::wstring deep(70,L'[');deep+=L"0";deep+=std::wstring(70,L']');
    assert(!PhoneLink::Parse(deep,reading));

    // French card (Mobile connecté), layout taken from a real Galaxy S26 card: the level sits on an
    // Image tooltip and on a Container whose TextBlock reads "56 %" (space before "%").
    const std::wstring french = LR"({
        "speak": "S26 de Max - Connecté à votre PC",
        "body": [{"type": "ColumnSet", "columns": [
            {"type": "Column", "items": [{"type": "Image", "tooltip": "Batterie 56 % restante, économie d'énergie"}]},
            {"type": "Column", "items": [{"type": "Container", "tooltip": "Batterie 56 % restante, économie d'énergie",
                "items": [{"type": "TextBlock", "text": "56 %"}]}]}
        ]}]
    })";
    reading = {};
    assert(PhoneLink::Parse(french, reading));
    assert(reading.name == L"S26 de Max");
    assert(reading.percent == 56);
    assert(!reading.charging);

    reading = {};
    assert(PhoneLink::Parse(LR"({"speak":"Pixel - Connecté à votre PC","tooltip":"Batterie 80 % restante, en charge","text":"80 %"})", reading));
    assert(reading.percent == 80 && reading.charging);
    // An English battery tooltip on a French card is not trusted, and neither is a French
    // disconnected card.
    assert(!PhoneLink::Parse(LR"({"speak":"Pixel - Connecté à votre PC","tooltip":"Battery 80% remaining"})", reading));
    assert(!PhoneLink::Parse(LR"({"speak":"Pixel - Déconnecté","tooltip":"Batterie 80 % restante"})", reading));
    // English with an extra state after "remaining".
    assert(PhoneLink::Parse(LR"({"speak":"Pixel - Connected to your PC","tooltip":"Battery 30% remaining, battery saver, charging"})", reading));
    assert(reading.percent == 30 && reading.charging);

    std::puts("Phone Link structural and localization tests passed");
    return 0;
}
