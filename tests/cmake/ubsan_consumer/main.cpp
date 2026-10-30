#include <array>
#include <inputnext_adapters/body_program.hpp>

#if INPUTNEXTADAPTERS_UBSAN_TRIGGER_OVERFLOW
#    include <climits>
#endif

int main() {
    using namespace inputnext::adapters;

    BodyProgram const empty{};
    std::array<float, 2> samples{1.0F, 1.0F};
    const auto window = build_body_pcm(empty, MonoTime{}, samples);
    if (window.frame_count != 1 || samples[0] != 0.0F || samples[1] != 0.0F) {
        return 1;
    }

#if INPUTNEXTADAPTERS_UBSAN_TRIGGER_OVERFLOW
    volatile int operand = 1;
    volatile int overflow = operand + INT_MAX;
    return overflow == 0 ? 0 : 2;
#else
    return 0;
#endif
}
