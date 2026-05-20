#pragma once

class MemoryStore
{
public:
    bool Open();
    void Append(std::wstring const& petId, std::wstring const& kind, std::wstring const& content);
    std::vector<std::wstring> LoadRecent(size_t maxItems) const;
    std::filesystem::path Path() const { return m_path; }

private:
    std::filesystem::path DataDirectory() const;
    static std::wstring EscapeJson(std::wstring const& value);
    static std::wstring NowIsoUtc();

    std::filesystem::path m_path;
};
