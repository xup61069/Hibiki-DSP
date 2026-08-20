#include "hibiki/control_service.hpp"

namespace hibiki {

bool ControlCommandQueueV1::try_push(const ControlCommandV1& command) noexcept {
    const auto head = head_.load(std::memory_order_relaxed);
    const auto tail = tail_.load(std::memory_order_acquire);
    if (head - tail >= kCapacity) {
        dropped_.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }
    slots_[static_cast<std::size_t>(head % kCapacity)] = command;
    head_.store(head + 1U, std::memory_order_release);
    return true;
}

bool ControlCommandQueueV1::try_pop(ControlCommandV1& command) noexcept {
    const auto tail = tail_.load(std::memory_order_relaxed);
    const auto head = head_.load(std::memory_order_acquire);
    if (tail == head) return false;
    command = slots_[static_cast<std::size_t>(tail % kCapacity)];
    tail_.store(tail + 1U, std::memory_order_release);
    return true;
}

bool enqueue_control_command_v1(const ControlCommandV1& command,
                                void* const context) noexcept {
    return context != nullptr &&
           static_cast<ControlCommandQueueV1*>(context)->try_push(command);
}

}  // namespace hibiki
