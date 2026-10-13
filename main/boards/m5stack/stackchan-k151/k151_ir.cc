// SPDX-License-Identifier: MIT
#include "k151_ir.h"
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>
#include <charconv>
#include <cstring>
#include "config.h"
#include "k151_ir_codec.h"
#include "mcp_server.h"

void K151Ir::Init() {
    rmt_tx_channel_config_t tx_config{};
    tx_config.gpio_num = K151_IR_TX_GPIO;
    tx_config.clk_src = RMT_CLK_SRC_DEFAULT;
    tx_config.resolution_hz = 1000000;
    tx_config.mem_block_symbols = 64;
    tx_config.trans_queue_depth = 1;
    rmt_rx_channel_config_t rx_config{};
    rx_config.gpio_num = K151_IR_RX_GPIO;
    rx_config.clk_src = RMT_CLK_SRC_DEFAULT;
    rx_config.resolution_hz = 1000000;
    rx_config.mem_block_symbols = 64;
    rmt_copy_encoder_config_t encoder_config{};
    if (rmt_new_tx_channel(&tx_config, &tx_) != ESP_OK ||
        rmt_new_rx_channel(&rx_config, &rx_) != ESP_OK ||
        rmt_new_copy_encoder(&encoder_config, &encoder_) != ESP_OK) {
        if (tx_)
            rmt_del_channel(tx_);
        if (rx_)
            rmt_del_channel(rx_);
        tx_ = rx_ = nullptr;
        ESP_LOGW("K151IR", "RMT initialization failed; IR tools disabled");
        return;
    }
    rmt_rx_event_callbacks_t callbacks{};
    callbacks.on_recv_done = [](rmt_channel_handle_t, const rmt_rx_done_event_data_t* data,
                                void* arg) -> bool {
        static_cast<K151Ir*>(arg)->received_.store(data->num_symbols + 1,
                                                   std::memory_order_release);
        return false;
    };
    if (rmt_rx_register_event_callbacks(rx_, &callbacks, this) != ESP_OK ||
        rmt_enable(tx_) != ESP_OK || rmt_enable(rx_) != ESP_OK) {
        ESP_LOGW("K151IR", "RMT enable failed");
        return;
    }
    commands_ = xQueueCreate(4, sizeof(Command));
    if (!commands_)
        return;
    available_ = true;
    if (xTaskCreate([](void* p) { static_cast<K151Ir*>(p)->Run(); }, "k151-ir", 4096, this, 2,
                    nullptr) != pdPASS) {
        available_ = false;
        return;
    }
}
void K151Ir::Rearm() {
    rmt_receive_config_t config{};
    config.signal_range_min_ns = 100000;
    config.signal_range_max_ns =
        12000000;  // Demodulated receiver; long AC gaps are outside this basic scope.
    if (rmt_receive(rx_, rx_buffer_.data(), sizeof(rx_buffer_), &config) != ESP_OK)
        ESP_LOGW("K151IR", "Could not arm receiver");
}
K151Ir::Frame K151Ir::Last() {
    std::lock_guard lock(mutex_);
    return last_;
}
bool K151Ir::Enqueue(const Command& command) {
    return available_ && xQueueSend(commands_, &command, 0) == pdTRUE;
}
bool K151Ir::Transmit(const Frame& frame) {
    if (!k151::ValidateRaw(frame.raw.data(), frame.count) || frame.carrier_khz < 30 ||
        frame.carrier_khz > 60)
        return false;
    rmt_carrier_config_t carrier{};
    carrier.frequency_hz = frame.carrier_khz * 1000;
    carrier.duty_cycle = 0.33f;
    if (rmt_apply_carrier(tx_, &carrier) != ESP_OK)
        return false;
    std::array<rmt_symbol_word_t, 128> symbols{};
    size_t count = 0;
    for (size_t i = 0; i < frame.count; i += 2) {
        symbols[count].level0 = 1;
        symbols[count].duration0 = frame.raw[i];
        symbols[count].level1 = 0;
        symbols[count].duration1 = i + 1 < frame.count ? frame.raw[i + 1] : 1;
        ++count;
    }
    rmt_transmit_config_t config{};
    return rmt_transmit(tx_, encoder_, symbols.data(), count * sizeof(symbols[0]), &config) ==
               ESP_OK &&
           rmt_tx_wait_all_done(tx_, 500) == ESP_OK;
}
bool K151Ir::Save(int slot, const Frame& frame) {
    if (slot < 0 || slot > 3 || !k151::ValidateRaw(frame.raw.data(), frame.count))
        return false;
    nvs_handle_t handle;
    if (nvs_open("k151_ir", NVS_READWRITE, &handle) != ESP_OK)
        return false;
    char key[16];
    snprintf(key, sizeof(key), "v1_slot%d", slot);
    // Fixed versioned layout; reject invalid blobs instead of replaying arbitrary memory.
    auto err = nvs_set_blob(handle, key, &frame, sizeof(frame));
    if (err == ESP_OK)
        err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}
bool K151Ir::Load(int slot, Frame& frame) {
    if (slot < 0 || slot > 3)
        return false;
    nvs_handle_t handle;
    if (nvs_open("k151_ir", NVS_READONLY, &handle) != ESP_OK)
        return false;
    char key[16];
    snprintf(key, sizeof(key), "v1_slot%d", slot);
    size_t bytes = sizeof(frame);
    auto err = nvs_get_blob(handle, key, &frame, &bytes);
    nvs_close(handle);
    return err == ESP_OK && bytes == sizeof(frame) && frame.carrier_khz >= 30 &&
           frame.carrier_khz <= 60 && k151::ValidateRaw(frame.raw.data(), frame.count);
}
void K151Ir::Run() {
    Rearm();
    while (true) {
        const auto received = received_.exchange(0, std::memory_order_acquire);
        if (received) {
            const auto count = received - 1;  // Zero-symbol completions must also rearm RX.
            Frame frame;
            bool mark_started = false;
            auto append = [&](uint16_t duration, bool level) {
                // Active-low demodulated input. Drop idle high before the first mark and final long
                // idle.
                if (!mark_started && level)
                    return;
                mark_started = true;
                if (duration < 50 || (level && duration >= 11000))
                    return;
                if (frame.count < MaxDurations)
                    frame.raw[frame.count++] = duration;
            };
            for (size_t i = 0; i < count && i < rx_buffer_.size(); ++i) {
                append(rx_buffer_[i].duration0, rx_buffer_[i].level0);
                append(rx_buffer_[i].duration1, rx_buffer_[i].level1);
            }
            std::lock_guard lock(mutex_);
            if (count < rx_buffer_.size() && frame.count == 0) {
                capture_status_ = "empty receive ignored; previous frame retained";
            } else if (count < rx_buffer_.size() &&
                       k151::IsNecRepeat(frame.raw.data(), frame.count)) {
                capture_status_ = "NEC repeat ignored; previous frame retained";
            } else if (count < rx_buffer_.size() &&
                       k151::ValidateRaw(frame.raw.data(), frame.count)) {
                last_ = frame;
                capture_status_ = "received bounded raw frame";
            } else {
                last_ = {};
                capture_status_ = "capture rejected: truncated or invalid";
            }
            Rearm();
        }
        Command command;
        if (xQueueReceive(commands_, &command, pdMS_TO_TICKS(10)) == pdTRUE) {
            bool ok = false;
            switch (command.type) {
                case Command::Send:
                    ok = Transmit(command.frame);
                    break;
                case Command::Learn:
                    ok = Save(command.slot, command.frame);
                    break;
                case Command::Replay:
                    ok = Load(command.slot, command.frame) && Transmit(command.frame);
                    break;
            }
            std::lock_guard lock(mutex_);
            result_ = ok ? "operation completed" : "operation failed";
        }
    }
}
void K151Ir::RegisterTools() {
    if (!available_)
        return;
    auto& server = McpServer::GetInstance();
    server.AddTool(
        "self.robot.ir_send_nec",
        "Queue basic NEC infrared send at 38kHz. Address and command are 8-bit; inverse bytes are "
        "generated.",
        PropertyList({Property("address", kPropertyTypeInteger, 0, 255),
                      Property("command", kPropertyTypeInteger, 0, 255)}),
        [this](const PropertyList& p) -> ReturnValue {
            Command cmd{};
            cmd.type = Command::Send;
            cmd.frame.count = 67;
            auto raw = k151::NecDurations(p["address"].value<int>(), p["command"].value<int>());
            std::copy(raw.begin(), raw.end(), cmd.frame.raw.begin());
            return Enqueue(cmd) ? std::string("queued; inspect ir_capture for completion")
                                : std::string("queue unavailable");
        });
    server.AddTool("self.robot.ir_capture",
                   "Read the last complete raw frame and basic NEC decode; unknown protocols "
                   "remain raw. Receiver cannot measure carrier frequency.",
                   {}, [this](const PropertyList&) -> ReturnValue {
                       std::lock_guard lock(mutex_);
                       auto json = cJSON_CreateObject();
                       cJSON_AddStringToObject(json, "operation", result_.c_str());
                       cJSON_AddStringToObject(json, "capture_status", capture_status_.c_str());
                       auto raw = cJSON_AddArrayToObject(json, "durations_us");
                       for (size_t i = 0; i < last_.count; ++i)
                           cJSON_AddItemToArray(raw, cJSON_CreateNumber(last_.raw[i]));
                       uint8_t address, command;
                       if (k151::DecodeNec(last_.raw.data(), last_.count, address, command)) {
                           cJSON_AddStringToObject(json, "protocol", "NEC");
                           cJSON_AddNumberToObject(json, "address", address);
                           cJSON_AddNumberToObject(json, "command", command);
                       } else
                           cJSON_AddStringToObject(json, "protocol", "raw/unknown");
                       return json;
                   });
    server.AddTool("self.robot.ir_learn_last",
                   "Queue storage of the last valid raw frame in one of four NVS slots. Supply "
                   "carrier kHz from the remote's specification; 38kHz is a default assumption.",
                   PropertyList({Property("slot", kPropertyTypeInteger, 0, 3),
                                 Property("carrier_khz", kPropertyTypeInteger, 38, 30, 60)}),
                   [this](const PropertyList& p) -> ReturnValue {
                       Command cmd{};
                       cmd.type = Command::Learn;
                       cmd.slot = p["slot"].value<int>();
                       cmd.frame = Last();
                       cmd.frame.carrier_khz = p["carrier_khz"].value<int>();
                       if (!k151::ValidateRaw(cmd.frame.raw.data(), cmd.frame.count))
                           return std::string("no valid frame");
                       return Enqueue(cmd)
                                  ? std::string("storage queued; inspect ir_capture for completion")
                                  : std::string("queue unavailable");
                   });
    server.AddTool("self.robot.ir_replay",
                   "Queue replay of a learned slot. Basic raw learning does not implement the "
                   "factory IR App or air-conditioner state protocols.",
                   PropertyList({Property("slot", kPropertyTypeInteger, 0, 3)}),
                   [this](const PropertyList& p) -> ReturnValue {
                       Command cmd{};
                       cmd.type = Command::Replay;
                       cmd.slot = p["slot"].value<int>();
                       return Enqueue(cmd);
                   });
    server.AddTool(
        "self.robot.ir_send_raw",
        "Queue raw alternating mark/space durations in microseconds, comma separated; at most 256 "
        "values and 300ms.",
        PropertyList({Property("durations_us", kPropertyTypeString).SetMaxLength(1536),
                      Property("carrier_khz", kPropertyTypeInteger, 38, 30, 60)}),
        [this](const PropertyList& p) -> ReturnValue {
            Command cmd{};
            cmd.type = Command::Send;
            cmd.frame.carrier_khz = p["carrier_khz"].value<int>();
            const auto& text = p["durations_us"].value<std::string>();
            const char* begin = text.data();
            const char* end = begin + text.size();
            while (begin < end && cmd.frame.count < MaxDurations) {
                unsigned value;
                auto parsed = std::from_chars(begin, end, value);
                if (parsed.ec != std::errc{} || value > 30000)
                    return std::string("invalid durations");
                cmd.frame.raw[cmd.frame.count++] = value;
                begin = parsed.ptr;
                if (begin < end && *begin++ != ',')
                    return std::string("expected comma");
            }
            if (begin != end || !k151::ValidateRaw(cmd.frame.raw.data(), cmd.frame.count))
                return std::string("invalid or oversized frame");
            return Enqueue(cmd);
        });
}
