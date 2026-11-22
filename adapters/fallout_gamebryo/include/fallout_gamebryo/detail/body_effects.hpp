#pragma once

#include <fallout_gamebryo/model.hpp>
#include <fallout_gamebryo/settings.hpp>

#include <inputnext_adapters/body_program.hpp>
#include <inputnext_adapters/output_frame.hpp>

#include <cstdint>

namespace fallout_gamebryo::detail {
class BodyEffects final {
public:
    void clear() noexcept;
    void reset_voice_ids() noexcept;
    void add(const Settings& settings,
        BodyEffect effect,
        MonoTime started_at,
        MonoTime now,
        float strength,
        std::uint64_t action) noexcept;
    void write(inputnext::adapters::OutputFrame& frame, MonoTime now) noexcept;

private:
    inputnext::adapters::BodyProgram active_;
    std::uint64_t next_voice_id_{1};
    std::uint64_t next_sequence_{1};
};
} // namespace fallout_gamebryo::detail
