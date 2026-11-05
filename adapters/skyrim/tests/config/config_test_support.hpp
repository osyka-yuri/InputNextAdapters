#pragma once
#include <memory>
#include <skyrim_adapter/native/config.hpp>

namespace skyrim_adapter::tests::config {
bool same_config(const skyrim_adapter::native::NativeConfig& left,
    const skyrim_adapter::native::NativeConfig& right);
std::unique_ptr<skyrim_adapter::native::NativeConfig> make_sample_prior_config();
} // namespace skyrim_adapter::tests::config
