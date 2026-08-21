// SPDX-License-Identifier: GPL-3.0-only

#include "hibiki/session_command_queue.hpp"

#include <new>

namespace hibiki {

SessionCommandQueueV1::SessionCommandQueueV1() noexcept
    : slots_(new (std::nothrow) std::array<SessionCommandWorkItemV1, kCapacity>{}) {}

bool SessionCommandQueueV1::try_push(const SessionCommandWorkItemV1& item) noexcept {
    if (slots_ == nullptr) {
        dropped_.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }
    const auto head = head_.load(std::memory_order_relaxed);
    const auto tail = tail_.load(std::memory_order_acquire);
    if (head - tail >= kCapacity) {
        dropped_.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }
    (*slots_)[static_cast<std::size_t>(head % kCapacity)] = item;
    head_.store(head + 1U, std::memory_order_release);
    return true;
}

bool SessionCommandQueueV1::try_push_volume(
    const SessionVolumeCommandV1& command) noexcept {
    SessionCommandWorkItemV1 item{};
    item.kind = SessionCommandKindV1::Volume;
    item.volume = command;
    return try_push(item);
}

bool SessionCommandQueueV1::try_push_route(
    const SessionRouteCommandV1& command) noexcept {
    SessionCommandWorkItemV1 item{};
    item.kind = SessionCommandKindV1::Route;
    item.route = command;
    return try_push(item);
}

bool SessionCommandQueueV1::try_push_route_rule(
    const SessionRouteRuleCommandV1& command) noexcept {
    SessionCommandWorkItemV1 item{};
    item.kind = SessionCommandKindV1::RouteRule;
    item.route_rule = command;
    return try_push(item);
}

bool SessionCommandQueueV1::try_pop(SessionCommandWorkItemV1& item) noexcept {
    if (slots_ == nullptr) return false;
    const auto tail = tail_.load(std::memory_order_relaxed);
    const auto head = head_.load(std::memory_order_acquire);
    if (tail == head) return false;
    item = (*slots_)[static_cast<std::size_t>(tail % kCapacity)];
    tail_.store(tail + 1U, std::memory_order_release);
    return true;
}

void SessionCommandQueueV1::reset() noexcept {
    // The runtime calls reset only after the pipe/control producer has been
    // stopped, so relaxed stores are sufficient and avoid a lifecycle lock.
    head_.store(0U, std::memory_order_relaxed);
    tail_.store(0U, std::memory_order_relaxed);
    dropped_.store(0U, std::memory_order_relaxed);
    if (slots_ != nullptr) {
        for (auto& slot : *slots_) slot = {};
    }
}

}  // namespace hibiki
