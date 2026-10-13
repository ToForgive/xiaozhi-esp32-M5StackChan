// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>

namespace k151 {
template <typename T>
class SensorCache {
public:
    void Update(T value, uint64_t now) {
        std::lock_guard lock(mutex_);
        value_ = std::move(value);
        updated_at_ = now;
    }
    void Invalidate() {
        std::lock_guard lock(mutex_);
        value_.reset();
    }
    std::optional<T> Read(uint64_t now, uint64_t max_age_ms) {
        std::lock_guard lock(mutex_);
        if (!value_ || now < updated_at_ || now - updated_at_ > max_age_ms)
            return std::nullopt;
        return value_;
    }

private:
    std::mutex mutex_;
    std::optional<T> value_;
    uint64_t updated_at_ = 0;
};
}  // namespace k151
