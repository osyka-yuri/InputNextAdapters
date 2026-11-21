#pragma once

#include <Windows.h>
#include <excpt.h>

#include <type_traits>

namespace oblivion_adapter::native {

[[nodiscard]] constexpr int memory_exception_filter(DWORD code) noexcept {
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR
               ? EXCEPTION_EXECUTE_HANDLER
               : EXCEPTION_CONTINUE_SEARCH;
}

// Keep borrowed SDK reads behind a call site so clang-cl's x86 SEH can catch their faults.
template <class Read>
[[nodiscard]] auto guarded_game_read(const Read& read) noexcept {
    using Result = decltype(read());
    static_assert(std::is_trivially_copyable_v<Result>);

    Result result{};
    __try {
#ifdef __clang__
        [[clang::noinline]]
#endif
        result = read();
    } __except (memory_exception_filter(GetExceptionCode())) {
        return Result{};
    }
    return result;
}

} // namespace oblivion_adapter::native
