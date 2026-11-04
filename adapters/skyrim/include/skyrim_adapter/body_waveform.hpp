#pragma once

#include <inputnext_adapters/body_program.hpp>

#include "model.hpp"

#include <span>

namespace skyrim_adapter {
inline constexpr std::uint32_t body_pcm_sample_rate = inputnext::adapters::body_pcm_sample_rate;
inline constexpr std::uint32_t body_pcm_max_frames = inputnext::adapters::body_pcm_max_frames;
using inputnext::adapters::BodyPcmWindow;

[[nodiscard]] inputnext::adapters::BodyProgram make_body_program(const BodyPlan& plan) noexcept;
[[nodiscard]] BodyPcmWindow build_body_pcm(const BodyPlan& plan,
    MonoTime begins_at,
    std::span<float> stereo_destination) noexcept;
} // namespace skyrim_adapter
