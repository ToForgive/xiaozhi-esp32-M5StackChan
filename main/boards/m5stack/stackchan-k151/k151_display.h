// SPDX-License-Identifier: MIT
#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include "display/lcd_display.h"
#include "k151_model.h"

class K151Display : public SpiLcdDisplay {
public:
    using SpiLcdDisplay::SpiLcdDisplay;
    void Start();
    bool SupportsGuiOperations() const override { return false; }
    void SetupUI() override;
    void SetStatus(const char* text) override;
    void ShowNotification(const char* text, int duration_ms = 3000) override;
    void ShowNotification(const std::string& text, int duration_ms = 3000) override {
        ShowNotification(text.c_str(), duration_ms);
    }
    void SetEmotion(const char* key) override;
    void SetChatMessage(const char* role, const char* text) override;
    void ClearChatMessages() override;
    void SetPowerSaveMode(bool on) override;
    void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
    void SetTheme(Theme* theme) override;
    void UpdateStatusBar(bool all = false) override;
    void SetTouchPoint(int count, int x, int y);
    void SetBatteryCallback(std::function<bool(int&, bool&, bool&)> callback) {
        battery_ = std::move(callback);
    }
    void SetWakeCallback(std::function<void()> callback) { wake_ = std::move(callback); }

private:
    struct Pending {
        std::string status = "Starting...", speech, notification;
        k151::Expression expression{k151::Emotion::Neutral, k151::Decoration::None};
        uint64_t notification_until = 0;
        uint32_t revision = 0;
        bool sleep = false;
    } pending_;
    std::mutex pending_mutex_;
    std::atomic<bool> requested_{false};
    std::atomic<uint32_t> touch_{0};
    std::function<void()> wake_;
    std::function<bool(int&, bool&, bool&)> battery_;
    void Run();
    void CreateAvatar();
    void ApplyReaction(k151::Reaction previous, k151::Reaction current);
    k151::Model model_;
    int blink_ = -1, idle_motion_ = -1, idle_expression_ = -1, speaking_ = -1;
    int decoration_ = -1, blush_ = -1;
    k151::Expression applied_{k151::Emotion::Neutral, k151::Decoration::None};
    uint32_t revision_ = UINT32_MAX;
    int saved_yaw_ = 0, saved_pitch_ = 0;
    uint8_t idle_motion_level_ = 2;
    uint64_t shake_mouth_tick_ = 0;
    lv_obj_t* system_label_ = nullptr;
    std::string speech_;
};
