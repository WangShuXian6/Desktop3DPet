#pragma once

struct AppConfig
{
    int petX = 1200;
    int petY = 520;
    int petWidth = 520;
    int petHeight = 520;
    float scale = 1.0f;
    bool alwaysOnTop = true;
    bool showPetOnStart = true;
    std::wstring modelPath = L"1.fbx";
    float modelRotationX = 0.0f;
    float modelRotationY = 0.0f;
    float modelRotationZ = 0.0f;
    float materialBrightness = 1.0f;
    float materialContrast = 1.0f;
    float materialSaturation = 1.0f;
    float materialAmbient = 0.22f;
    float materialDirectLight = 0.78f;
    float materialSpecular = 0.0f;
    float materialRoughness = 0.45f;
};

class ConfigStore
{
public:
    AppConfig Load();
    void Save(AppConfig const& config);
    std::wstring DataDirectory() const;

private:
    std::filesystem::path ConfigPath() const;
    static int ReadInt(std::wstring const& text, std::wstring const& key, int fallback);
    static float ReadFloat(std::wstring const& text, std::wstring const& key, float fallback);
    static bool ReadBool(std::wstring const& text, std::wstring const& key, bool fallback);
    static std::wstring ReadString(std::wstring const& text, std::wstring const& key, std::wstring const& fallback);
    static std::wstring EscapeJsonString(std::wstring const& value);
};
