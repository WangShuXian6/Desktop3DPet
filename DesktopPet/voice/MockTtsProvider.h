#pragma once
#include "TtsProvider.h"

class MockTtsProvider final : public ITtsProvider
{
public:
    SpeakResult Speak(SpeakRequest const& request) override;
};
