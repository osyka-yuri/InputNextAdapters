#include "support.hpp"
#include "support/test_runner.hpp"
#include <catch2/catch_session.hpp>

#include <Windows.h>

#include <array>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace inputnext::test::runtime_client {
namespace {
FixturePaths g_fixtures{};

std::string wide_to_utf8(const wchar_t* wstr) {
    if (wstr == nullptr || *wstr == L'\0') {
        return {};
    }
    const int size_needed = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (size_needed <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size_needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, result.data(), size_needed, nullptr, nullptr);
    return result;
}

void try_populate_fallback_fixtures(const std::filesystem::path& exe_path) {
    // If running manually from build tree, DLLs are in tests/
    const std::array<const wchar_t*, 16> kDefaultDllNames{
        L"fake_runtime_kind_0_id_1.dll",
        L"fake_runtime_kind_0_id_2.dll",
        L"fake_runtime_kind_1_id_3.dll",
        L"fake_runtime_kind_2_id_4.dll",
        L"fake_runtime_kind_3_id_5.dll",
        L"fake_runtime_kind_4_id_6.dll",
        L"fake_runtime_kind_5_id_7.dll",
        L"fake_runtime_kind_6_id_8.dll",
        L"fake_runtime_kind_7_id_9.dll",
        L"fake_runtime_kind_8_id_10.dll",
        L"fake_runtime_kind_9_id_11.dll",
        L"fake_runtime_kind_10_id_12.dll",
        L"fake_runtime_kind_11_id_13.dll",
        L"fake_runtime_kind_12_id_14.dll",
        L"fake_runtime_kind_13_id_15.dll",
        L"fake_runtime_kind_14_id_16.dll",
    };

    const auto exe_dir = exe_path.parent_path();
    const std::array<std::filesystem::path, 3> search_dirs{
        exe_dir,
        exe_dir / L".." / L".." / L".." / L"tests",
        exe_dir / L".." / L".." / L"tests",
    };

    for (const auto& dir : search_dirs) {
        if (std::filesystem::exists(dir / kDefaultDllNames[0])) {
            FixturePaths paths;
            for (std::size_t i = 0; i < 16; ++i) {
                paths[i] = std::filesystem::canonical(dir / kDefaultDllNames[i]);
            }
            g_fixtures = std::move(paths);
            return;
        }
    }
}

void parse_semicolon_fixtures(std::wstring_view arg,
    std::vector<std::filesystem::path>& parsed_fixtures) {
    std::size_t start = 0;
    while (start < arg.size()) {
        const auto sep = arg.find(L';', start);
        const auto token = arg.substr(start,
            sep == std::wstring_view::npos ? std::wstring_view::npos : sep - start);
        if (!token.empty() && token != L"--inputnext-fixtures") {
            parsed_fixtures.emplace_back(token);
        }
        if (sep == std::wstring_view::npos) {
            break;
        }
        start = sep + 1;
    }
}
} // namespace

const FixturePaths& get_fixtures() {
    return g_fixtures;
}

void set_fixtures(FixturePaths fixtures) {
    g_fixtures = std::move(fixtures);
}
} // namespace inputnext::test::runtime_client

namespace {
int run_tests(int argc, wchar_t* const* argv) {
    using namespace inputnext::test::runtime_client;

    std::vector<std::filesystem::path> parsed_fixtures;
    std::vector<std::string> catch_args_storage;
    catch_args_storage.reserve(static_cast<std::size_t>(argc));

    if (argc > 0) {
        catch_args_storage.push_back(wide_to_utf8(argv[0]));
    }

    for (int i = 1; i < argc; ++i) {
        const std::wstring_view arg{argv[i]};
        if (arg.starts_with(L"--inputnext-fixtures;")) {
            parse_semicolon_fixtures(arg, parsed_fixtures);
        } else if (std::wcscmp(argv[i], L"--inputnext-fixture") == 0 && i + 1 < argc) {
            parsed_fixtures.emplace_back(argv[++i]);
        } else if (std::wcscmp(argv[i], L"--inputnext-fixtures") == 0) {
            while (i + 1 < argc && parsed_fixtures.size() < 16) {
                if (argv[i + 1][0] == L'-' && argv[i + 1][1] == L'-') {
                    break;
                }
                parsed_fixtures.emplace_back(argv[++i]);
            }
        } else {
            catch_args_storage.push_back(wide_to_utf8(argv[i]));
        }
    }

    if (parsed_fixtures.size() == 16) {
        FixturePaths fixtures;
        for (std::size_t i = 0; i < 16; ++i) {
            fixtures[i] = std::move(parsed_fixtures[i]);
        }
        set_fixtures(std::move(fixtures));
    } else if (argc > 0) {
        try_populate_fallback_fixtures(argv[0]);
    }

    std::vector<char*> catch_argv;
    catch_argv.reserve(catch_args_storage.size());
    for (auto& argument : catch_args_storage) {
        catch_argv.push_back(argument.data());
    }

    Catch::Session session;
    return session.run(static_cast<int>(catch_argv.size()), catch_argv.data());
}
} // namespace

extern "C" int wmain(int argc, wchar_t* const* argv) {
    return inputnext::test::run_main("runtime_client", [&] { return run_tests(argc, argv); });
}
