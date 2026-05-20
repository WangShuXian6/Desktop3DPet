#include "pch.h"
#include "ConfigStore.h"

std::wstring ConfigStore::DataDirectory() const
{
    wchar_t buffer[MAX_PATH]{};
    DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    std::filesystem::path dir = count > 0 ? std::filesystem::path(buffer) : std::filesystem::temp_directory_path();
    dir /= L"DesktopPetWinUI3";
    std::filesystem::create_directories(dir);
    return dir.wstring();
}

std::filesystem::path ConfigStore::ConfigPath() const
{
    return std::filesystem::path(DataDirectory()) / L"config.json";
}

AppConfig ConfigStore::Load()
{
    AppConfig cfg{};
    auto path = ConfigPath();
    if (!std::filesystem::exists(path))
    {
        Save(cfg);
        return cfg;
    }

    std::wifstream in(path);
    std::wstringstream ss;
    ss << in.rdbuf();
    std::wstring text = ss.str();

    cfg.petX = ReadInt(text, L"petX", cfg.petX);
    cfg.petY = ReadInt(text, L"petY", cfg.petY);
    cfg.petWidth = ReadInt(text, L"petWidth", cfg.petWidth);
    cfg.petHeight = ReadInt(text, L"petHeight", cfg.petHeight);
    cfg.scale = ReadFloat(text, L"scale", cfg.scale);
    cfg.alwaysOnTop = ReadBool(text, L"alwaysOnTop", cfg.alwaysOnTop);
    cfg.showPetOnStart = ReadBool(text, L"showPetOnStart", cfg.showPetOnStart);
    cfg.modelPath = ReadString(text, L"modelPath", cfg.modelPath);
    cfg.modelRotationX = ReadFloat(text, L"modelRotationX", cfg.modelRotationX);
    cfg.modelRotationY = ReadFloat(text, L"modelRotationY", cfg.modelRotationY);
    cfg.modelRotationZ = ReadFloat(text, L"modelRotationZ", cfg.modelRotationZ);
    cfg.materialBrightness = ReadFloat(text, L"materialBrightness", cfg.materialBrightness);
    cfg.materialContrast = ReadFloat(text, L"materialContrast", cfg.materialContrast);
    cfg.materialSaturation = ReadFloat(text, L"materialSaturation", cfg.materialSaturation);
    cfg.materialAmbient = ReadFloat(text, L"materialAmbient", cfg.materialAmbient);
    cfg.materialDirectLight = ReadFloat(text, L"materialDirectLight", cfg.materialDirectLight);
    cfg.materialSpecular = ReadFloat(text, L"materialSpecular", cfg.materialSpecular);
    cfg.materialRoughness = ReadFloat(text, L"materialRoughness", cfg.materialRoughness);
    return cfg;
}

void ConfigStore::Save(AppConfig const& cfg)
{
    std::filesystem::create_directories(std::filesystem::path(DataDirectory()));
    std::wofstream out(ConfigPath(), std::ios::trunc);
    out << L"{\n";
    out << L"  \"petX\": " << cfg.petX << L",\n";
    out << L"  \"petY\": " << cfg.petY << L",\n";
    out << L"  \"petWidth\": " << cfg.petWidth << L",\n";
    out << L"  \"petHeight\": " << cfg.petHeight << L",\n";
    out << L"  \"scale\": " << cfg.scale << L",\n";
    out << L"  \"alwaysOnTop\": " << (cfg.alwaysOnTop ? L"true" : L"false") << L",\n";
    out << L"  \"showPetOnStart\": " << (cfg.showPetOnStart ? L"true" : L"false") << L",\n";
    out << L"  \"modelPath\": \"" << EscapeJsonString(cfg.modelPath) << L"\",\n";
    out << L"  \"modelRotationX\": " << cfg.modelRotationX << L",\n";
    out << L"  \"modelRotationY\": " << cfg.modelRotationY << L",\n";
    out << L"  \"modelRotationZ\": " << cfg.modelRotationZ << L",\n";
    out << L"  \"materialBrightness\": " << cfg.materialBrightness << L",\n";
    out << L"  \"materialContrast\": " << cfg.materialContrast << L",\n";
    out << L"  \"materialSaturation\": " << cfg.materialSaturation << L",\n";
    out << L"  \"materialAmbient\": " << cfg.materialAmbient << L",\n";
    out << L"  \"materialDirectLight\": " << cfg.materialDirectLight << L",\n";
    out << L"  \"materialSpecular\": " << cfg.materialSpecular << L",\n";
    out << L"  \"materialRoughness\": " << cfg.materialRoughness << L"\n";
    out << L"}\n";
}

int ConfigStore::ReadInt(std::wstring const& text, std::wstring const& key, int fallback)
{
    size_t pos = text.find(std::wstring(L"\"") + key + L"\"");
    if (pos == std::wstring::npos) return fallback;
    pos = text.find(L":", pos);
    if (pos == std::wstring::npos) return fallback;
    try { return std::stoi(text.substr(pos + 1)); } catch (...) { return fallback; }
}

float ConfigStore::ReadFloat(std::wstring const& text, std::wstring const& key, float fallback)
{
    size_t pos = text.find(std::wstring(L"\"") + key + L"\"");
    if (pos == std::wstring::npos) return fallback;
    pos = text.find(L":", pos);
    if (pos == std::wstring::npos) return fallback;
    try { return std::stof(text.substr(pos + 1)); } catch (...) { return fallback; }
}

bool ConfigStore::ReadBool(std::wstring const& text, std::wstring const& key, bool fallback)
{
    size_t pos = text.find(std::wstring(L"\"") + key + L"\"");
    if (pos == std::wstring::npos) return fallback;
    pos = text.find(L":", pos);
    if (pos == std::wstring::npos) return fallback;
    auto tail = text.substr(pos + 1, 16);
    if (tail.find(L"true") != std::wstring::npos) return true;
    if (tail.find(L"false") != std::wstring::npos) return false;
    return fallback;
}

std::wstring ConfigStore::ReadString(std::wstring const& text, std::wstring const& key, std::wstring const& fallback)
{
    size_t pos = text.find(std::wstring(L"\"") + key + L"\"");
    if (pos == std::wstring::npos) return fallback;
    pos = text.find(L":", pos);
    if (pos == std::wstring::npos) return fallback;
    pos = text.find(L"\"", pos);
    if (pos == std::wstring::npos) return fallback;

    std::wstring value;
    bool escaping = false;
    for (size_t i = pos + 1; i < text.size(); ++i)
    {
        wchar_t ch = text[i];
        if (escaping)
        {
            switch (ch)
            {
            case L'"': value.push_back(L'"'); break;
            case L'\\': value.push_back(L'\\'); break;
            case L'n': value.push_back(L'\n'); break;
            case L'r': value.push_back(L'\r'); break;
            case L't': value.push_back(L'\t'); break;
            default: value.push_back(ch); break;
            }
            escaping = false;
            continue;
        }

        if (ch == L'\\')
        {
            escaping = true;
            continue;
        }

        if (ch == L'"')
        {
            return value;
        }

        value.push_back(ch);
    }

    return fallback;
}

std::wstring ConfigStore::EscapeJsonString(std::wstring const& value)
{
    std::wstring escaped;
    for (wchar_t ch : value)
    {
        switch (ch)
        {
        case L'"': escaped += L"\\\""; break;
        case L'\\': escaped += L"\\\\"; break;
        case L'\n': escaped += L"\\n"; break;
        case L'\r': escaped += L"\\r"; break;
        case L'\t': escaped += L"\\t"; break;
        default: escaped.push_back(ch); break;
        }
    }
    return escaped;
}
