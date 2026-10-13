// SPDX-License-Identifier: MIT
#pragma once
#include <esp_timer.h>
#include <functional>
#include "k151_power_policy.h"

class K151PowerSaveTimer {
public:
    K151PowerSaveTimer(int sleep_seconds, int shutdown_seconds)
        : policy_(sleep_seconds, shutdown_seconds) {}
    void WakeUp() { policy_.WakeUp(); }
    void SetEnabled(bool enabled) { policy_.SetEnabled(enabled); }
    void OnEnterSleepMode(std::function<void()> callback) { enter_ = std::move(callback); }
    void OnExitSleepMode(std::function<void()> callback) { exit_ = std::move(callback); }
    void OnShutdownRequest(std::function<void()> callback) { shutdown_ = std::move(callback); }
    // Hardware task only; no ESP timer task accesses this policy or its callbacks.
    void Poll(bool can_sleep) {
        switch (policy_.Poll(esp_timer_get_time() / 1000, can_sleep)) {
            case k151::PowerPolicy::EnterSleep:
                if (enter_)
                    enter_();
                break;
            case k151::PowerPolicy::ExitSleep:
                if (exit_)
                    exit_();
                break;
            case k151::PowerPolicy::Shutdown:
                if (shutdown_)
                    shutdown_();
                break;
            default:
                break;
        }
    }

private:
    k151::PowerPolicy policy_;
    std::function<void()> enter_, exit_, shutdown_;
};
