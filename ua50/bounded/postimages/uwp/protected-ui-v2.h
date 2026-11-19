#pragma once
#include <atomic>

namespace xllama::bridge::protected_ui_v2 {
// Latched before Application::Start. Deferred UI callbacks read the same value.
inline std::atomic<bool> selected{false};
inline void latch() noexcept { selected.store(true, std::memory_order_release); }
inline bool active() noexcept { return selected.load(std::memory_order_acquire); }
}
