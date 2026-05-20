#include "pch.h"
#include "MemoryStore.h"

bool MemoryStore::Open()
{
    auto dir = DataDirectory();
    std::filesystem::create_directories(dir);
    m_path = dir / L"memory.jsonl";
    if (!std::filesystem::exists(m_path))
    {
        std::wofstream out(m_path);
    }
    return std::filesystem::exists(m_path);
}

std::filesystem::path MemoryStore::DataDirectory() const
{
    wchar_t buffer[MAX_PATH]{};
    DWORD count = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    std::filesystem::path dir = count > 0 ? std::filesystem::path(buffer) : std::filesystem::temp_directory_path();
    dir /= L"DesktopPetWinUI3";
    return dir;
}

void MemoryStore::Append(std::wstring const& petId, std::wstring const& kind, std::wstring const& content)
{
    if (m_path.empty()) Open();
    std::wofstream out(m_path, std::ios::app);
    out << L"{\"time\":\"" << EscapeJson(NowIsoUtc()) << L"\","
        << L"\"petId\":\"" << EscapeJson(petId) << L"\","
        << L"\"kind\":\"" << EscapeJson(kind) << L"\","
        << L"\"content\":\"" << EscapeJson(content) << L"\"}\n";
}

std::vector<std::wstring> MemoryStore::LoadRecent(size_t maxItems) const
{
    std::vector<std::wstring> lines;
    if (m_path.empty() || !std::filesystem::exists(m_path)) return lines;
    std::wifstream in(m_path);
    std::wstring line;
    while (std::getline(in, line))
    {
        if (!line.empty()) lines.push_back(line);
    }
    if (lines.size() > maxItems)
    {
        lines.erase(lines.begin(), lines.end() - static_cast<std::ptrdiff_t>(maxItems));
    }
    return lines;
}

std::wstring MemoryStore::EscapeJson(std::wstring const& value)
{
    std::wstring out;
    out.reserve(value.size());
    for (wchar_t ch : value)
    {
        switch (ch)
        {
        case L'\\': out += L"\\\\"; break;
        case L'\"': out += L"\\\""; break;
        case L'\n': out += L"\\n"; break;
        case L'\r': out += L"\\r"; break;
        case L'\t': out += L"\\t"; break;
        default: out += ch; break;
        }
    }
    return out;
}

std::wstring MemoryStore::NowIsoUtc()
{
    SYSTEMTIME st{};
    GetSystemTime(&st);
    wchar_t buf[64]{};
    swprintf_s(buf, L"%04u-%02u-%02uT%02u:%02u:%02uZ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}
