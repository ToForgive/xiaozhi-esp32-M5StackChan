// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <cstdint>

namespace k151 {
// Poll is owned by the hardware task. Producers submit atomic requests only.
class PowerPolicy {
public:
    enum Action { None, EnterSleep, ExitSleep, Shutdown };
    PowerPolicy(int sleep_seconds, int shutdown_seconds)
        : sleep_ms_(sleep_seconds < 0 ? 0 : uint64_t(sleep_seconds) * 1000),
          shutdown_ms_(shutdown_seconds < 0 ? 0 : uint64_t(shutdown_seconds) * 1000) {}
    void WakeUp() { wake_.store(true); }
    void SetEnabled(bool enabled) { enabled_.store(enabled); }
    Action Poll(uint64_t now, bool can_sleep) {
        if (shutdown_)
            return None;
        const bool wake = wake_.exchange(false);
        if (!started_ || wake || !enabled_.load() || !can_sleep) {
            last_activity_ = now;
            started_ = true;
            if (sleeping_) {
                sleeping_ = false;
                return ExitSleep;
            }
            return None;
        }
        const uint64_t elapsed = now - last_activity_;
        if (shutdown_ms_ && elapsed >= shutdown_ms_) {
            shutdown_ = true;
            return Shutdown;
        }
        if (!sleeping_ && sleep_ms_ && elapsed >= sleep_ms_) {
            sleeping_ = true;
            return EnterSleep;
        }
        return None;
    }

private:
    std::atomic<bool> wake_{false}, enabled_{false};
    uint64_t sleep_ms_, shutdown_ms_, last_activity_ = 0;
    bool started_ = false, sleeping_ = false, shutdown_ = false;
};
}  // namespace k151
