// SPDX-License-Identifier: MIT
#include "k151_display.h"
#include <esp_lvgl_port.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include <cstring>
#include "hal/hal.h"
#include "k151_application.h"
#include "lvgl_font.h"
#include "lvgl_theme.h"
#include "stackchan/modifiers/blink.h"
#include "stackchan/modifiers/breath.h"
#include "stackchan/modifiers/idle_expression.h"
#include "stackchan/modifiers/idle_motion.h"
#include "stackchan/modifiers/speaking.h"
#include "stackchan/stackchan.h"

using namespace stackchan;
using namespace stackchan::avatar;
namespace {
std::string Bounded(const char* text) {
    if (!text)
        return {};
    size_t n = strnlen(text, 1025);
    if (n > 1024) {
        n = 1024;
        while (n && (static_cast<unsigned char>(text[n]) & 0xc0) == 0x80)
            --n;
    }
    return std::string(text, n);
}
uint64_t Now() { return esp_timer_get_time() / 1000; }
}  // namespace

void K151Display::Start() {
    requested_theme_.store(current_theme_);
    ESP_ERROR_CHECK(xTaskCreatePinnedToCore([](void* p) { static_cast<K151Display*>(p)->Run(); },
                                            "k151-avatar", 8192, this, 3, nullptr, 1) == pdPASS
                        ? ESP_OK
                        : ESP_ERR_NO_MEM);
}
void K151Display::SetupUI() {
    if (requested_.load())
        return;
    // Startup-only creation. The running speech loop only submits snapshots.
    DisplayLockGuard lock(this);
    if (!lock)
        return;
    CreateAvatar();
    requested_.store(true);
}
void K151Display::SetStatus(const char* text) {
    std::lock_guard lock(pending_mutex_);
    pending_.status = Bounded(text);
    ++pending_.revision;
}
void K151Display::ShowNotification(const char* text, int duration_ms) {
    std::lock_guard lock(pending_mutex_);
    pending_.notification = Bounded(text);
    pending_.notification_until = Now() + std::clamp(duration_ms, 0, 60000);
    ++pending_.revision;
}
void K151Display::SetEmotion(const char* key) {
    std::lock_guard lock(pending_mutex_);
    pending_.expression = k151::MapEmotion(key ? key : "neutral");
    ++pending_.revision;
}
void K151Display::SetChatMessage(const char* role, const char* text) {
    if (!role)
        return;
    std::lock_guard lock(pending_mutex_);
    if (std::string_view(role) == "system" || std::string_view(role) == "assistant") {
        pending_.speech = Bounded(text);
        ++pending_.revision;
    }
}
void K151Display::ClearChatMessages() { SetChatMessage("system", ""); }
void K151Display::SetPowerSaveMode(bool on) {
    std::lock_guard lock(pending_mutex_);
    pending_.sleep = on;
    if (on)
        pending_.speech.clear();
    ++pending_.revision;
}
void K151Display::SetPreviewImage(std::unique_ptr<LvglImage> image) {
    std::lock_guard lock(pending_mutex_);
    pending_preview_ = std::move(image);
    preview_changed_ = true;
}
void K151Display::UpdateStatusBar(bool all) {
    // The worker renders a bounded status snapshot; callers never wait on animation.
    (void)all;
}
void K151Display::SetTheme(Theme* theme) {
    if (!theme)
        return;
    auto font = theme->GetTextFont();
    std::lock_guard lock(pending_mutex_);
    pending_font_ = std::move(font);
    theme_changed_ = true;
    // The application owns theme selection; the worker only consumes its font snapshot.
    current_theme_ = theme;
    requested_theme_.store(theme);
}
void K151Display::ApplyFont(std::shared_ptr<LvglFont> font) {
    const lv_font_t* face_font = font ? font->font() : &lv_font_montserrat_16;
    GetStackChan().avatar().setSpeechTextFont(const_cast<lv_font_t*>(face_font));
    for (auto label : {status_label_, notification_label_, system_label_}) {
        if (label)
            lv_obj_set_style_text_font(label, face_font, 0);
    }
    // Keep the previous font alive until all styles have rebound, including delayed retries.
    rendered_font_ = std::move(font);
}
void K151Display::ApplyPreview(std::unique_ptr<LvglImage> image, uint64_t now) {
    // K151 owns this object; the default chat UI's emoji_box_/content_ never exist here.
    if (!image || !image->image_dsc()->header.w || !image->image_dsc()->header.h) {
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        lv_image_set_src(preview_image_, nullptr);
        preview_image_cached_.reset();
        preview_until_ = 0;
        return;
    }
    auto dsc = image->image_dsc();
    lv_image_set_src(preview_image_, dsc);
    const uint32_t scale = std::min(320u * 256 / dsc->header.w, 240u * 256 / dsc->header.h);
    lv_image_set_scale(preview_image_, std::max<uint32_t>(1, scale));
    lv_obj_remove_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    preview_image_cached_ = std::move(image);
    preview_until_ = now + 10000;
    // Labels were created after the preview and stay above it for activation/error text.
}
void K151Display::SetTouchPoint(int count, int x, int y) {
    if (count && x >= 0 && x < 320 && y >= 0 && y < 240)
        touch_.store(0x80000000u | (uint32_t(x) << 16) | uint32_t(y));
    else
        touch_.store(0);
}

void K151Display::CreateAvatar() {
    auto& robot = GetStackChan();
    auto avatar = std::make_unique<DefaultAvatar>();
    auto font = GetTheme()->GetTextFont();
    rendered_font_ = font;
    avatar->init(lv_screen_active(), font ? font->font() : &lv_font_montserrat_16);
    avatar->getPanel()->onClick().connect([this]() {
        static uint32_t last_click = 0;
        const auto now = GetHAL().millis();
        if (last_click != 0 && now - last_click < 2000)
            return;
        last_click = now;
        if (wake_)
            wake_();
        // LVGL callback does not mutate the application directly.
        K151ToggleChat();
    });
    robot.attachAvatar(std::move(avatar));
    idle_motion_level_ = GetHAL().getConfig().idleRandomMovementLevel;
    robot.addModifier(std::make_unique<BreathModifier>());
    blink_ = robot.addModifier(std::make_unique<BlinkModifier>());
    // System prompts and status remain visible above the unchanged factory face.
    auto top = lv_layer_top();
    preview_image_ = lv_image_create(top);
    lv_obj_set_size(preview_image_, 320, 240);
    lv_obj_align(preview_image_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    const lv_font_t* text_font = font ? font->font() : &lv_font_montserrat_16;
    auto label = [top, text_font](lv_align_t align, int x, int y) {
        auto obj = lv_label_create(top);
        lv_obj_set_style_text_color(obj, lv_color_white(), 0);
        lv_obj_set_style_text_font(obj, text_font, 0);
        lv_obj_set_style_bg_color(obj, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_80, 0);
        lv_obj_set_width(obj, 300);
        lv_label_set_long_mode(obj, LV_LABEL_LONG_MODE_SCROLL_CIRCULAR);
        lv_obj_align(obj, align, x, y);
        lv_label_set_text(obj, "");
        return obj;
    };
    status_label_ = label(LV_ALIGN_TOP_MID, 0, 1);
    notification_label_ = label(LV_ALIGN_TOP_MID, 0, 1);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
    system_label_ = label(LV_ALIGN_BOTTOM_MID, 0, -1);
    // Common status updater tolerates absent optional labels only through our custom updater below.
    auto indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_display(indev, display_);
    lv_indev_set_user_data(indev, this);
    lv_indev_set_read_cb(indev, [](lv_indev_t* input, lv_indev_data_t* data) {
        auto self = static_cast<K151Display*>(lv_indev_get_user_data(input));
        auto packed = self->touch_.load();
        data->state = packed >> 31 ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        data->point.x = (packed >> 16) & 0x7fff;
        data->point.y = packed & 0xffff;
    });
    Display::SetupUI();
}

void K151Display::ApplyReaction(k151::Reaction previous, k151::Reaction current) {
    auto& robot = GetStackChan();
    auto& face = robot.avatar();
    auto& motion = robot.motion();
    face.setModifyLock(current == k151::Reaction::Shake);
    motion.setModifyLock(current != k151::Reaction::None || model_.Sleeping());
    face.leftEye().setVisible(current != k151::Reaction::Shake);
    face.rightEye().setVisible(current != k151::Reaction::Shake);
    face.mouth().setRotation(0);
    face.mouth().setWeight(0);
    if (previous == k151::Reaction::None && current == k151::Reaction::Pet) {
        auto angles =
            motion.getCurrentAngles();  // Cached physical feedback; no UART under LVGL lock.
        saved_yaw_ = angles.x;
        saved_pitch_ = angles.y;
    }
    if (current == k151::Reaction::Pet) {
        auto& random = Random::getInstance();
        int yaw = saved_yaw_, pitch = saved_pitch_;
        switch (random.getInt(0, 2)) {
            case 0:
                pitch += random.getInt(150, 250);
                yaw += random.getInt(-50, 50);
                break;
            case 1:
                pitch -= random.getInt(0, 50);
                yaw += random.getInt(0, 1) == 0 ? 150 : -150;
                break;
            default:
                pitch += random.getInt(250, 400);
                break;
        }
        motion.moveWithSpeed(std::clamp(yaw, -512, 512), std::clamp(pitch, 50, 540),
                             random.getInt(300, 500));
    } else if (current == k151::Reaction::Shake) {
        motion.goHome(300);
    } else if (previous == k151::Reaction::Pet && model_.Interactive() && !model_.Sleeping()) {
        motion.moveWithSpeed(saved_yaw_, saved_pitch_, 200);
    }
    static_cast<BlinkModifier*>(robot.getModifier(blink_))->resyncEyeWeights();
}

void K151Display::Run() {
    auto& robot = GetStackChan();
    DeviceState last_state = kDeviceStateUnknown;
    bool last_sleep = false;
    k151::Reaction applied_reaction = k151::Reaction::None;
    uint64_t last_status = 0;
    std::unique_ptr<LvglImage> next_preview;
    std::shared_ptr<LvglFont> next_font;
    bool preview_dirty = false, font_dirty = false;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(20));
        if (!requested_.load())
            continue;
        Pending input;
        {
            std::lock_guard lock(pending_mutex_);
            input = pending_;
            if (preview_changed_) {
                next_preview = std::move(pending_preview_);
                preview_changed_ = false;
                preview_dirty = true;
            }
            if (theme_changed_) {
                next_font = pending_font_;
                theme_changed_ = false;
                font_dirty = true;
            }
        }
        const auto state = K151DeviceState();
        const uint64_t now = Now();

        model_.SetState(state);
        model_.SetSleep(input.sleep);
        model_.SetExpression(input.expression);
        auto events = GetHAL().takeEvents();
        if (events && wake_)
            wake_();
        if (events & Hal::Pet)
            model_.Pet(now);
        if (events & Hal::Release)
            model_.Release(now);
        if (events & Hal::Shake)
            model_.Shake(now);
        model_.Tick(now);
        const bool pet_refresh = (events & Hal::Pet) && model_.GetReaction() == k151::Reaction::Pet;
        // All UART/I2C actuator work is outside the display lock and outside application/audio
        // tasks.
        GetHAL().updateMotion();
        if (state != last_state) {
            GetHAL().showRgbColor(0, 0, 0);
            if (state == kDeviceStateListening)
                GetHAL().setRgbColor(0, 0, 50, 0);
            if (state == kDeviceStateSpeaking)
                GetHAL().setRgbColor(0, 0, 0, 50);
            GetHAL().refreshRgb();
        }
        if (input.sleep != last_sleep)
            GetHAL().requestSleep(input.sleep);
        if (!Lock(10))
            continue;
        // Retain submitted images/fonts across lock failure; never mutate LVGL without the lock.
        if (font_dirty) {
            ApplyFont(std::move(next_font));
            font_dirty = false;
        }
        if (preview_dirty) {
            ApplyPreview(std::move(next_preview), now);
            preview_dirty = false;
        } else if (preview_until_ && now >= preview_until_) {
            ApplyPreview(nullptr, now);
        }
        auto& face = robot.avatar();
        // Sleep owns the motion lock even with no temporary reaction; wake must release it.
        if (applied_reaction != model_.GetReaction() || pet_refresh || input.sleep != last_sleep) {
            ApplyReaction(applied_reaction, model_.GetReaction());
            applied_reaction = model_.GetReaction();
        }
        auto expression = model_.Visible();
        if (expression.emotion != applied_.emotion ||
            expression.decoration != applied_.decoration || state != last_state ||
            input.sleep != last_sleep || pet_refresh) {
            face.setEmotion(expression.emotion);
            face.removeDecorator(decoration_);
            face.removeDecorator(blush_);
            decoration_ = blush_ = -1;
            // Persistent base decoration is finite and refreshed only when expression changes.
            switch (expression.decoration) {
                case k151::Decoration::Heart:
                    decoration_ = face.addDecorator(
                        std::make_unique<HeartDecorator>(lv_screen_active(), 2500, 500));
                    break;
                case k151::Decoration::Angry:
                    decoration_ = face.addDecorator(
                        std::make_unique<AngryDecorator>(lv_screen_active(), 4000));
                    break;
                case k151::Decoration::Sweat:
                    decoration_ = face.addDecorator(
                        std::make_unique<SweatDecorator>(lv_screen_active(), 4000));
                    break;
                case k151::Decoration::Shy:
                    decoration_ =
                        face.addDecorator(std::make_unique<ShyDecorator>(lv_screen_active(), 4000));
                    break;
                case k151::Decoration::Dizzy:
                    decoration_ = face.addDecorator(std::make_unique<DizzyDecorator>(
                        lv_screen_active(),
                        model_.GetReaction() == k151::Reaction::Shake ? 0 : 4000, 300));
                    break;
                default:
                    break;
            }
            if (model_.GetReaction() != k151::Reaction::None)
                blush_ = face.addDecorator(std::make_unique<ShyDecorator>(
                    lv_screen_active(), model_.GetReaction() == k151::Reaction::Shake ? 0 : 2500));
            applied_ = expression;
            static_cast<BlinkModifier*>(robot.getModifier(blink_))->resyncEyeWeights();
        }
        if (model_.Speaking() && speaking_ < 0)
            speaking_ = robot.addModifier(std::make_unique<SpeakingModifier>(0, 180, false));
        if (!model_.Speaking() && speaking_ >= 0) {
            robot.removeModifier(speaking_);
            speaking_ = -1;
            face.mouth().setWeight(0);
        }
        if (model_.Idle() && idle_expression_ < 0) {
            idle_expression_ = robot.addModifier(std::make_unique<IdleExpressionModifier>());
            switch (idle_motion_level_) {
                case 0:
                    break;
                case 1:
                    idle_motion_ =
                        robot.addModifier(std::make_unique<IdleMotionModifier>(8000, 12000));
                    break;
                case 3:
                    idle_motion_ =
                        robot.addModifier(std::make_unique<IdleMotionModifier>(2000, 4000));
                    break;
                default:
                    idle_motion_ = robot.addModifier(std::make_unique<IdleMotionModifier>());
                    break;
            }
        }
        if (!model_.Idle()) {
            if (idle_expression_ >= 0) {
                face.leftEye().setPosition({0, 0});
                face.rightEye().setPosition({0, 0});
                face.mouth().setPosition({0, 0});
                face.mouth().setRotation(0);
            }
            robot.removeModifier(idle_motion_);
            idle_motion_ = -1;
            robot.removeModifier(idle_expression_);
            idle_expression_ = -1;
        }
        if (model_.GetReaction() == k151::Reaction::Shake && now >= shake_mouth_tick_) {
            face.mouth().setWeight(65);
            face.mouth().setRotation((now / 600) % 2 ? 25 : 3575);
            shake_mouth_tick_ = now + 600;
        }
        if (input.revision != revision_) {
            lv_label_set_text(status_label_, input.status.c_str());
            face.setSpeech(input.speech);
            // System/activation text also has an unobstructed independent overlay.
            lv_label_set_text(system_label_, input.speech.c_str());
            revision_ = input.revision;
        }
        bool notification = !input.notification.empty() && now < input.notification_until;
        lv_label_set_text(notification_label_, notification ? input.notification.c_str() : "");
        if (notification) {
            lv_obj_remove_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
        }
        if (model_.Sleeping())
            face.setSpeech("Zzz…");
        else if (last_sleep)
            face.setSpeech(input.speech);
        robot.updateVisual();
        Unlock();
        last_state = state;
        last_sleep = input.sleep;
        if (now - last_status > 1000) {
            last_status = now;
            // Small bounded status snapshot. No fabricated charging animation.
            int level;
            bool charging, discharging;
            if (battery_ && battery_(level, charging, discharging)) {
                std::string status =
                    input.status + " | " + std::to_string(level) + "%" + (charging ? " +" : "");
                if (Lock(10)) {
                    lv_label_set_text(status_label_, status.c_str());
                    Unlock();
                }
            }
        }
    }
}
