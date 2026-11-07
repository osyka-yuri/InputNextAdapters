#include "native/diagnostics.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <sal.h>
#include <span>
#include <string_view>

namespace {
class AllocationState final {
public:
    static void start_monitoring() noexcept {
        monitoring_ = true;
        count_ = 0;
    }

    static void stop_monitoring() noexcept { monitoring_ = false; }

    [[nodiscard]] static bool is_monitoring() noexcept { return monitoring_; }

    static void record_allocation() noexcept { ++count_; }

    [[nodiscard]] static std::size_t count() noexcept { return count_; }

private:
    static inline bool monitoring_{false};
    static inline std::size_t count_{0};
};

class ScopedAllocationMonitor final {
public:
    ScopedAllocationMonitor() noexcept { AllocationState::start_monitoring(); }

    ~ScopedAllocationMonitor() noexcept { AllocationState::stop_monitoring(); }

    ScopedAllocationMonitor(const ScopedAllocationMonitor&) = delete;
    ScopedAllocationMonitor& operator=(const ScopedAllocationMonitor&) = delete;
    ScopedAllocationMonitor(ScopedAllocationMonitor&&) = delete;
    ScopedAllocationMonitor& operator=(ScopedAllocationMonitor&&) = delete;

    [[nodiscard]] static std::size_t count() noexcept { return AllocationState::count(); }
};

using skyrim_adapter::native::diagnostics::detail::Hex32;
using skyrim_adapter::native::diagnostics::detail::LogLine;

static_assert(noexcept(LogLine{}.append("text")));
static_assert(noexcept(LogLine{}.append(std::uint64_t{})));
static_assert(noexcept(LogLine{}.append(Hex32{})));
static_assert(noexcept(LogLine{}.finish()));

[[noreturn]] void fail(const char* message) noexcept {
    static_cast<void>(std::fputs("diagnostics test failed: ", stderr));
    static_cast<void>(std::fputs(message, stderr));
    static_cast<void>(std::fputc('\n', stderr));
    std::exit(EXIT_FAILURE);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

void expect_line(skyrim_adapter::native::diagnostics::detail::LogLine& line,
    std::string_view expected,
    const char* message) {
    const auto actual = line.finish();
    require(actual == expected, message);
    // The backing buffer includes a NUL beyond the returned text view.
    const std::span terminated_text{actual.data(), actual.size() + 1};
    require(terminated_text.back() == '\0', "line is NUL terminated");
}

void test_integer_composition() {
    LogLine line;
    line.append("unsigned=");
    line.append(std::numeric_limits<std::uint64_t>::max());
    line.append(" signed_min=");
    line.append(std::numeric_limits<std::int64_t>::min());
    line.append(" signed_max=");
    line.append(std::numeric_limits<std::int64_t>::max());
    expect_line(line,
        "unsigned=18446744073709551615 signed_min=-9223372036854775808 "
        "signed_max=9223372036854775807\n",
        "integer values preserve their complete decimal range");
}

void test_hex_and_bounded_text() {
    constexpr std::array menu_name{'I', 'n', 'v', 'e', 'n', 't', 'o', 'r', 'y'};
    LogLine line;
    line.append("menu=");
    line.append(std::string_view{menu_name.data(), menu_name.size()});
    line.append(" flags=0x");
    line.append(Hex32{0x12AB00FEU});
    expect_line(line,
        "menu=Inventory flags=0x12AB00FE\n",
        "bounded text and uppercase fixed-width hex compose exactly");

    constexpr std::array embedded_zero{'a', 'b', '\0', 'c', 'd'};
    LogLine nul_terminated_text;
    nul_terminated_text.append(std::string_view{embedded_zero.data(), embedded_zero.size()});
    expect_line(nul_terminated_text,
        "ab\n",
        "text composition keeps C-string termination behavior");
}

void test_empty_and_existing_newline() {
    LogLine empty;
    expect_line(empty, "\n", "empty messages receive a newline");

    LogLine already_terminated;
    already_terminated.append("already terminated\n");
    expect_line(already_terminated, "already terminated\n", "an existing newline is retained once");
}

void test_capacity_boundaries() {
    std::array<char, 510> exact_payload{};
    exact_payload.fill('e');
    LogLine exact_fit;
    exact_fit.append(std::string_view{exact_payload.data(), exact_payload.size()});
    const auto exact_text = exact_fit.finish();
    require(exact_text.size() == 511, "510 text bytes fit with newline and terminator");
    require(exact_text.substr(0, exact_payload.size()) ==
                std::string_view{exact_payload.data(), exact_payload.size()},
        "exact-fit payload remains intact");
    require(exact_text.back() == '\n' &&
                std::span{exact_text.data(), exact_text.size() + 1}.back() == '\0',
        "exact-fit line ends with newline and NUL");

    std::array<char, 511> full_payload{};
    full_payload.fill('t');
    LogLine truncated;
    truncated.append(std::string_view{full_payload.data(), full_payload.size()});
    const auto truncated_text = truncated.finish();
    require(truncated_text.size() == 511, "full buffer remains within the line capacity");
    require(truncated_text.substr(0, 510) == std::string_view{full_payload.data(), 510},
        "truncation retains the first 510 payload bytes");
    require(truncated_text.back() == '\n' &&
                std::span{truncated_text.data(), truncated_text.size() + 1}.back() == '\0',
        "truncation reserves newline and NUL");

    full_payload.back() = '\n';
    LogLine newline_at_capacity;
    newline_at_capacity.append(std::string_view{full_payload.data(), full_payload.size()});
    require(newline_at_capacity.finish() ==
                std::string_view{full_payload.data(), full_payload.size()},
        "an exact-capacity message already ending in newline is preserved");

    std::array<char, 520> oversized_payload{};
    oversized_payload.fill('x');
    LogLine parts_after_capacity;
    parts_after_capacity.append(
        std::string_view{oversized_payload.data(), oversized_payload.size()});
    parts_after_capacity.append("unwritten suffix");
    const auto composed_text = parts_after_capacity.finish();
    require(composed_text.size() == 511 && composed_text.back() == '\n',
        "later parts cannot exceed the fixed buffer");
    require(composed_text.substr(0, 510) == std::string_view{oversized_payload.data(), 510},
        "oversized composition follows the existing truncation boundary");
}

void test_no_allocations() {
    std::array<char, 2> observed{};
    {
        const ScopedAllocationMonitor monitor;
        LogLine line;
        line.append("value=");
        line.append(std::numeric_limits<std::uint64_t>::max());
        line.append(" flags=0x");
        line.append(Hex32{0xFFFFFFFFU});
        const auto text = line.finish();
        observed = {text.front(), text.back()};
        require(ScopedAllocationMonitor::count() == 0,
            "fixed-buffer formatting performs no operator new allocations");
    }
    require(observed[0] == 'v' && observed[1] == '\n', "allocation check formats a complete line");
}

void test_allocation_trap() {
    bool caught_bad_alloc = false;
    void* unexpected_memory = nullptr;
    {
        const ScopedAllocationMonitor monitor;
        try {
            unexpected_memory = ::operator new(sizeof(int));
        } catch (const std::bad_alloc&) {
            caught_bad_alloc = true;
        } catch (...) {
            fail("unexpected non-bad_alloc exception during allocation trap");
        }
        require(caught_bad_alloc, "direct operator new call while monitoring triggers bad_alloc");
        require(ScopedAllocationMonitor::count() == 1,
            "direct operator new call increments the allocation counter");
    }

    if (unexpected_memory != nullptr) {
        ::operator delete(unexpected_memory, sizeof(int));
    }

    void* normal_memory = ::operator new(sizeof(int));
    require(normal_memory != nullptr,
        "direct operator new returns a valid pointer when monitoring is disabled");
    ::operator delete(normal_memory, sizeof(int));

    // Also verify std::nothrow overload trapping
    {
        const ScopedAllocationMonitor monitor;
        const void* nothrow_ptr = ::operator new(sizeof(int), std::nothrow);
        require(nothrow_ptr == nullptr, "nothrow operator new returns nullptr while monitoring");
        require(ScopedAllocationMonitor::count() == 1,
            "nothrow operator new increments the allocation counter");
    }
}

void test_order_independence() {
    // Verify that running allocation checks multiple times or after traps leaves no residual state
    require(!AllocationState::is_monitoring(), "monitoring is inactive before test");
    test_no_allocations();
    require(!AllocationState::is_monitoring(), "monitoring is inactive after test_no_allocations");
    test_allocation_trap();
    require(!AllocationState::is_monitoring(), "monitoring is inactive after test_allocation_trap");
    test_no_allocations();
    require(!AllocationState::is_monitoring(),
        "monitoring remains inactive after repeated execution");
}
} // namespace

#ifdef _MSC_VER
_Use_decl_annotations_
#endif
    void* operator new(std::size_t size) {
    if (AllocationState::is_monitoring()) {
        AllocationState::record_allocation();
        throw std::bad_alloc{};
    }
    if (auto* memory = std::malloc(size == 0 ? 1 : size)) {
        return memory;
    }
    throw std::bad_alloc{};
}

#ifdef _MSC_VER
_Use_decl_annotations_
#endif
    void* operator new[](std::size_t size) {
    return ::operator new(size);
}

#ifdef _MSC_VER
_Use_decl_annotations_
#endif
    void* operator new(std::size_t size, const std::nothrow_t& tag) noexcept {
    static_cast<void>(tag);
    if (AllocationState::is_monitoring()) {
        AllocationState::record_allocation();
        return nullptr;
    }
    return std::malloc(size == 0 ? 1 : size);
}

#ifdef _MSC_VER
_Use_decl_annotations_
#endif
    void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept {
    static_cast<void>(tag);
    return ::operator new(size, std::nothrow);
}

void operator delete(void* block) noexcept {
    std::free(block);
}

void operator delete(void* block, std::size_t size) noexcept {
    static_cast<void>(size);
    std::free(block);
}

void operator delete[](void* block) noexcept {
    ::operator delete(block);
}

void operator delete[](void* block, std::size_t size) noexcept {
    static_cast<void>(size);
    ::operator delete(block);
}

void operator delete(void* block, const std::nothrow_t& tag) noexcept {
    static_cast<void>(tag);
    std::free(block);
}

void operator delete[](void* block, const std::nothrow_t& tag) noexcept {
    static_cast<void>(tag);
    std::free(block);
}

int main() {
    test_integer_composition();
    test_hex_and_bounded_text();
    test_empty_and_existing_newline();
    test_capacity_boundaries();
    test_no_allocations();
    test_allocation_trap();
    test_order_independence();
    return EXIT_SUCCESS;
}
