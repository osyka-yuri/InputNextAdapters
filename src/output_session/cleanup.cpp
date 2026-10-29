#include <inputnext_adapters/output_session.hpp>

#include "internal.hpp"

#include <input_next_core.h>
#include <input_next_effects.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace inputnext::adapters {
using namespace detail;

bool OutputSession::retain_cleanup(const Binding& binding) noexcept {
    for (auto& pending : pending_cleanup_) {
        if (pending.active) {
            continue;
        }
        pending = PendingCleanup{
            .context = binding.context,
            .target = binding.target,
            .active = true,
        };
        return true;
    }
    return false;
}

bool OutputSession::close_binding(input_next_status status, bool notify_observer) noexcept {
    if (!binding_) {
        return true;
    }
    auto& binding = *binding_;
    binding.context_state = INPUT_NEXT_EFFECTS_CONTEXT_REVOKED;
    if (!retain_cleanup(binding)) {
        diagnostics_.last_status = INPUT_NEXT_STATUS_CAPACITY_EXCEEDED;
        return false;
    }
    if (body_owner_ && equal_context(body_owner_->context, binding.context)) {
        body_owner_.reset();
    }
    binding_.reset();
    reset_attempt_history();
    reset_accepted_fallback();
    if (notify_observer) {
        observer_.binding_lost(status);
    }
    return true;
}

void OutputSession::advance_cleanup_record(PendingCleanup& pending) noexcept {
    if (!pending.active) {
        return;
    }
    if (pending.has_receipt) {
        const auto status = output_.ack_close(pending.receipt);
        diagnostics_.last_status = status;
        if (status == INPUT_NEXT_STATUS_OK || status == INPUT_NEXT_STATUS_STALE_CONTEXT) {
            pending = {};
        }
        return;
    }

    input_next_effects_context_info_v1 info{};
    info.struct_size = static_cast<std::uint32_t>(sizeof(info));
    info.context.struct_size = static_cast<std::uint32_t>(sizeof(info.context));
    const auto query_status = output_.query_context(pending.context, info);
    if (query_status == INPUT_NEXT_STATUS_STALE_CONTEXT) {
        // The runtime session retired the exact handle; no retained cleanup record can survive it.
        pending = {};
        diagnostics_.last_status = query_status;
        return;
    }
    if (query_status == INPUT_NEXT_STATUS_OK || query_status == INPUT_NEXT_STATUS_REVOKED) {
        if (info.struct_size != sizeof(info) || !equal_context(info.context, pending.context) ||
            info.state < INPUT_NEXT_EFFECTS_CONTEXT_ATTACHING ||
            info.state > INPUT_NEXT_EFFECTS_CONTEXT_REVOKED) {
            diagnostics_.last_status = INPUT_NEXT_STATUS_INVALID_ARGUMENT;
            return;
        }
    }

    input_next_effects_close_v1 request{};
    request.struct_size = static_cast<std::uint32_t>(sizeof(request));
    request.context = pending.context;
    request.close_request_id = pending.context.id;
    input_next_effects_close_receipt_v1 receipt{};
    receipt.struct_size = static_cast<std::uint32_t>(sizeof(receipt));
    const auto close_status = output_.close_context(request, receipt);
    diagnostics_.last_status = close_status;
    if (close_status == INPUT_NEXT_STATUS_STALE_CONTEXT) {
        pending = {};
        return;
    }
    if (close_status != INPUT_NEXT_STATUS_REVOKED) {
        return;
    }
    if (!valid_close_receipt(pending.context, pending.target, receipt)) {
        diagnostics_.last_status = INPUT_NEXT_STATUS_INVALID_ARGUMENT;
        return;
    }
    pending.receipt = receipt;
    pending.has_receipt = true;
    const auto ack_status = output_.ack_close(pending.receipt);
    diagnostics_.last_status = ack_status;
    if (ack_status == INPUT_NEXT_STATUS_OK || ack_status == INPUT_NEXT_STATUS_STALE_CONTEXT) {
        pending = {};
    }
}

void OutputSession::advance_pending_cleanup(std::size_t max_records) noexcept {
    std::size_t processed{};
    for (std::size_t visited = 0; visited < pending_cleanup_.size() && processed < max_records;
        ++visited) {
        auto& pending = pending_cleanup_[next_cleanup_];
        next_cleanup_ = (next_cleanup_ + 1) % pending_cleanup_.size();
        if (pending.active) {
            advance_cleanup_record(pending);
            ++processed;
        }
    }
}

void OutputSession::maintain_cleanup() noexcept {
    advance_pending_cleanup();
}

bool OutputSession::close_and_drain([[maybe_unused]] TickClock& clock) noexcept {
    const bool binding_retained = close_binding(INPUT_NEXT_STATUS_OK, false);
    advance_pending_cleanup(cleanup_capacity);
    if (!binding_retained || binding_) {
        return false;
    }
    return std::ranges::all_of(pending_cleanup_,
        [](const PendingCleanup& pending) noexcept { return !pending.active; });
}

void OutputSession::expire_admission(TickClock& clock) noexcept {
    static_cast<void>(clock);
    const auto prior_status = diagnostics_.last_status;
    observer_.admission_expired();
    // Admission loss revokes the exact session through reserved cleanup capacity. Ordinary
    // clears depend on foreground/admission and can fail while the context remains live.
    if (close_binding(INPUT_NEXT_STATUS_NOT_ADMITTED, false)) {
        advance_pending_cleanup();
        if (prior_status != INPUT_NEXT_STATUS_OK) {
            diagnostics_.last_status = prior_status;
        }
    }
}

} // namespace inputnext::adapters
