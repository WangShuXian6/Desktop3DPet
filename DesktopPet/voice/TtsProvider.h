#pragma once

struct SpeakRequest
{
    std::wstring text;
    std::wstring voice;
    std::wstring style;
};

struct SpeakResult
{
    bool ok = true;
    std::wstring message;
};

class ITtsProvider
{
public:
    virtual ~ITtsProvider() = default;
    virtual SpeakResult Speak(SpeakRequest const& request) = 0;
};
