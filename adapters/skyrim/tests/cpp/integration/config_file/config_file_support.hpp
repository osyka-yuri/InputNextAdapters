#pragma once
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>

namespace skyrim_adapter::tests::config_file {
using ConfigCatalogue = std::map<std::string, std::set<std::string>>;
bool extract_catalogue(std::string_view contents, ConfigCatalogue& catalogue);
bool read_text_file(const std::filesystem::path& path, std::string& contents);
ConfigCatalogue expected_reference_catalogue();

class TemporaryConfigDirectory {
public:
    TemporaryConfigDirectory() = default;
    TemporaryConfigDirectory(const TemporaryConfigDirectory&) = delete;
    TemporaryConfigDirectory& operator=(const TemporaryConfigDirectory&) = delete;
    TemporaryConfigDirectory(TemporaryConfigDirectory&&) = delete;
    TemporaryConfigDirectory& operator=(TemporaryConfigDirectory&&) = delete;
    ~TemporaryConfigDirectory() noexcept;
    bool create();
    [[nodiscard]] std::filesystem::path file(std::string_view name) const;
    explicit operator bool() const noexcept;

private:
    std::filesystem::path path_;
    bool owns_directory_{};
};
} // namespace skyrim_adapter::tests::config_file
