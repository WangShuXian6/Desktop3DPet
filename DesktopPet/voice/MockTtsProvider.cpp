#include "pch.h"
#include "MockTtsProvider.h"

SpeakResult MockTtsProvider::Speak(SpeakRequest const& request)
{
    if (!request.text.empty())
    {
        MessageBeep(MB_ICONINFORMATION);
    }
    SpeakResult result{};
    result.ok = true;
    result.message = std::wstring(L"Mock TTS 已触发：") + request.text;
    return result;
}
