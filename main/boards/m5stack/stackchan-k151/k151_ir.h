// SPDX-License-Identifier: MIT
#pragma once
#include <driver/rmt_encoder.h>
#include <driver/rmt_rx.h>
#include <driver/rmt_tx.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <array>
#include <atomic>
#include <mutex>
#include <string>

class K151Ir {
public:
    static constexpr size_t MaxDurations = 256;
    struct Frame {
        uint16_t count = 0, carrier_khz = 38;
        std::array<uint16_t, MaxDurations> raw{};
    };
    void Init();
    void RegisterTools();
    bool Available() const { return available_; }
    Frame Last();

private:
    struct Command {
        enum Type { Send, Learn, Replay } type;
        int slot = 0;
        Frame frame;
    };
    bool Enqueue(const Command& command);
    void Run();
    bool Transmit(const Frame& frame);
    bool Save(int slot, const Frame& frame);
    bool Load(int slot, Frame& frame);
    void Rearm();
    rmt_channel_handle_t tx_ = nullptr, rx_ = nullptr;
    rmt_encoder_handle_t encoder_ = nullptr;
    std::array<rmt_symbol_word_t, 128> rx_buffer_{};
    std::atomic<size_t> received_{0};
    QueueHandle_t commands_ = nullptr;
    std::mutex mutex_;
    Frame last_;
    std::string result_ = "no operation";
    std::string capture_status_ = "no capture";
    bool available_ = false;
};
inline K151Ir& GetK151Ir() {
    static K151Ir value;
    return value;
}
