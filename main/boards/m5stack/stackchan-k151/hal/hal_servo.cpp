/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <esp_log.h>
#include <settings.h>
#include <stackchan/stackchan.h>
#include <smooth_ui_toolkit.hpp>
#include "config.h"
#include "drivers/FTServo_Arduino/src/SCSCL.h"
#include "hal.h"

using namespace smooth_ui_toolkit;
using namespace stackchan::motion;

static SCSCL _scs_bus;

struct ServoConfig_t {
    int id = -1;
    int defaultZeroPos = 0;
    Vector2i angleLimit;
    Vector2i rawPosLimit;
    std::string settingNs;
    std::string settingZeroPositionKey;
    bool enablePwmMode = false;
    bool enableStallProtection = false;
};

class ScsServo : public Servo {
public:
    static inline const std::string _tag = "ScsServo";

    ScsServo(const ServoConfig_t& config)
        : _config(config), _runtime_raw_pos_limit(config.rawPosLimit) {}

    void init() override {
        reset_runtime_limits();
        set_angle_limit(_config.angleLimit);
        get_zero_pos_from_nvs();
        Servo::init();
    }

    void get_zero_pos_from_nvs() {
        _zero_pos = _config.defaultZeroPos;
        bool is_valid = false;

        {
            Settings settings(_config.settingNs, false);
            int nvs_zero_pos = settings.GetInt(_config.settingZeroPositionKey, -1);

            // Limit check
            if (nvs_zero_pos >= _config.rawPosLimit.x && nvs_zero_pos <= _config.rawPosLimit.y) {
                _zero_pos = nvs_zero_pos;
                is_valid = true;
                ESP_LOGI("K151Servo", "ID %d calibrated zero=%d", _config.id, _zero_pos);
            } else {
                is_valid = false;
                ESP_LOGW("K151Servo", "ID %d invalid NVS zero=%d; keeping NVS unchanged",
                         _config.id, nvs_zero_pos);
            }
        }

        if (!is_valid) {
            _zero_pos = _config.defaultZeroPos;
            ESP_LOGW("K151Servo", "ID %d using boot-only zero=%d", _config.id, _zero_pos);

            // Leave existing/missing calibration untouched; explicit calibration saves it.
        }
    }

    void set_angle_impl(int angle) override {
        if (fault_latched_)
            return;
        if (!torque_enabled_)
            setTorqueEnabled(true);
        int mapped_angle = _zero_pos + angle * 16 / 5 / 10;  // 一步对应 0.3125度, 0.3125 = 5/16
        mapped_angle =
            uitk::clamp(mapped_angle, _runtime_raw_pos_limit.x, _runtime_raw_pos_limit.y);

        if (update_stall_protection(mapped_angle)) {
            return;
        }

        check_mode(Mode::Position);
        _scs_bus.WritePos(_config.id, mapped_angle, 20, 0);
    }

    void update() override {
        const uint32_t now = GetHAL().millis();
        if (now - feedback_tick_ >= 50) {
            feedback_tick_ = now;
            if (_scs_bus.FeedBack(_config.id) >= 0 && is_raw_pos_valid(_scs_bus.ReadPos(-1))) {
                cached_angle_ = uitk::clamp(raw_pos_to_angle(_scs_bus.ReadPos(-1)),
                                            getAngleLimit().x, getAngleLimit().y);
                cached_moving_ = _scs_bus.ReadMove(-1) > 0;
                feedback_failures_ = 0;
            } else if (++feedback_failures_ >= 3 && !fault_latched_) {
                fault_latched_ = true;
                stop_motion_at_angle(cached_angle_);
                setTorqueEnabled(false);
                ESP_LOGE("K151Servo", "ID %d feedback lost: motion disabled until reboot",
                         _config.id);
            }
        }
        if (!fault_latched_)
            Servo::update();
    }
    // Display modifiers only inspect cached feedback. UART waits run outside LVGL.
    int getCurrentAngle() override { return cached_angle_; }
    bool is_moving_impl() override { return cached_moving_; }
    void setTorqueEnabled(bool enabled) override {
        if (enabled && fault_latched_)
            return;
        _scs_bus.EnableTorque(_config.id, enabled ? 1 : 0);
        torque_enabled_ = enabled;
    }
    bool getTorqueEnabled() override { return torque_enabled_; }

    void setCurrentAngleAsZero() override {
        int current_pos = _scs_bus.ReadPos(_config.id);
        if (!is_raw_pos_valid(current_pos)) {
            ESP_LOGW("K151Servo", "ID %d invalid calibration read=%d; zero unchanged", _config.id,
                     current_pos);
            return;
        }

        _zero_pos = current_pos;
        reset_runtime_limits();

        Settings settings(_config.settingNs, true);
        settings.SetInt(_config.settingZeroPositionKey, _zero_pos);

        ESP_LOGI("K151Servo", "ID %d stored zero=%d", _config.id, _zero_pos);
    }

    void resetZeroCalibration() override {
        _zero_pos = _config.defaultZeroPos;
        reset_runtime_limits();

        Settings settings(_config.settingNs, true);
        settings.SetInt(_config.settingZeroPositionKey, _zero_pos);

        ESP_LOGI("K151Servo", "ID %d stored zero=%d", _config.id, _zero_pos);
    }

    void rotate(int velocity) override {
        velocity = uitk::clamp(velocity, -1000, 1000);

        if (!_config.enablePwmMode) {
            return;
        }

        int mapped_velocity = map_range(velocity, 0, 1000, 0, 1023);

        check_mode(Mode::PWM);
        _scs_bus.WritePWM(_config.id, mapped_velocity);
    }

private:
    enum class Mode { Position = 0, PWM = 1 };

    int cached_angle_ = 0;
    bool cached_moving_ = false, torque_enabled_ = false, fault_latched_ = false;
    uint32_t feedback_tick_ = 0;
    int feedback_failures_ = 0;
    ServoConfig_t _config;
    Vector2i _runtime_raw_pos_limit;
    int _zero_pos = 0;
    Mode _current_mode = Mode::Position;

    static constexpr uint32_t kStallFeedbackIntervalMs = 50;
    static constexpr int kStallMinTargetDeltaRaw = 8;
    static constexpr int kStallMaxPositionDeltaRaw = 1;
    static constexpr int kStallCurrentRiseThreshold = 80;
    static constexpr int kStallLoadRiseThreshold = 150;
    static constexpr int kStallCurrentAbsThreshold = 350;
    static constexpr int kStallLoadAbsThreshold = 650;
    static constexpr int kStallConfirmSamples = 2;

    uint32_t _last_stall_check_tick = 0;
    int _last_stall_raw_pos = 0;
    int _last_stall_current_abs = 0;
    int _last_stall_load_abs = 0;
    int _last_stall_direction = 0;
    int _stall_confirm_count = 0;
    bool _last_stall_feedback_valid = false;

    static int abs_int(int value) { return value < 0 ? -value : value; }

    bool is_raw_pos_valid(int raw_pos) const {
        return raw_pos >= _config.rawPosLimit.x && raw_pos <= _config.rawPosLimit.y;
    }

    int raw_pos_to_angle(int raw_pos) const { return (raw_pos - _zero_pos) * 5 * 10 / 16; }

    void reset_runtime_limits() {
        _runtime_raw_pos_limit = _config.rawPosLimit;
        set_angle_limit(_config.angleLimit);
        reset_stall_detection();
    }

    void reset_stall_detection() {
        _last_stall_feedback_valid = false;
        _last_stall_direction = 0;
        _stall_confirm_count = 0;
    }

    bool update_stall_protection(int target_raw_pos) {
        if (!_config.enableStallProtection) {
            return false;
        }

        const uint32_t now = GetHAL().millis();
        if (now - _last_stall_check_tick < kStallFeedbackIntervalMs) {
            return false;
        }
        _last_stall_check_tick = now;

        if (_scs_bus.FeedBack(_config.id) < 0) {
            reset_stall_detection();
            return false;
        }

        const int current_raw_pos = _scs_bus.ReadPos(-1);
        const int current_abs = abs_int(_scs_bus.ReadCurrent(-1));
        const int load_abs = abs_int(_scs_bus.ReadLoad(-1));

        if (!is_raw_pos_valid(current_raw_pos)) {
            reset_stall_detection();
            return false;
        }

        const int target_delta = target_raw_pos - current_raw_pos;
        if (abs_int(target_delta) < kStallMinTargetDeltaRaw) {
            reset_stall_detection();
            return false;
        }

        const int direction = target_delta > 0 ? 1 : -1;
        if (_last_stall_feedback_valid && direction == _last_stall_direction) {
            const int pos_delta = abs_int(current_raw_pos - _last_stall_raw_pos);
            const bool position_stuck = pos_delta <= kStallMaxPositionDeltaRaw;
            const bool current_spike =
                current_abs >= kStallCurrentAbsThreshold ||
                current_abs - _last_stall_current_abs >= kStallCurrentRiseThreshold;
            const bool load_spike = load_abs >= kStallLoadAbsThreshold ||
                                    load_abs - _last_stall_load_abs >= kStallLoadRiseThreshold;

            if (position_stuck && (current_spike || load_spike)) {
                _stall_confirm_count++;
            } else if (pos_delta > kStallMaxPositionDeltaRaw) {
                _stall_confirm_count = 0;
            }
        } else {
            _stall_confirm_count = 0;
        }

        _last_stall_raw_pos = current_raw_pos;
        _last_stall_current_abs = current_abs;
        _last_stall_load_abs = load_abs;
        _last_stall_direction = direction;
        _last_stall_feedback_valid = true;

        if (_stall_confirm_count < kStallConfirmSamples) {
            return false;
        }

        handle_stall(current_raw_pos, direction, current_abs, load_abs);
        return true;
    }

    void handle_stall(int raw_pos, int direction, int current_abs, int load_abs) {
        int angle = raw_pos_to_angle(raw_pos);
        angle = uitk::clamp(angle, _config.angleLimit.x, _config.angleLimit.y);

        auto angle_limit = getAngleLimit();
        if (direction > 0) {
            if (raw_pos < _runtime_raw_pos_limit.y) {
                _runtime_raw_pos_limit.y = raw_pos;
            }
            if (angle < angle_limit.y) {
                angle_limit.y = angle;
            }
        } else {
            if (raw_pos > _runtime_raw_pos_limit.x) {
                _runtime_raw_pos_limit.x = raw_pos;
            }
            if (angle > angle_limit.x) {
                angle_limit.x = angle;
            }
        }
        set_angle_limit(angle_limit);
        stop_motion_at_angle(angle);
        reset_stall_detection();

        check_mode(Mode::Position);
        _scs_bus.WritePos(_config.id, raw_pos, 20, 0);

        ESP_LOGW("K151Servo",
                 "ID %d stall: raw=%d angle=%d direction=%d current=%d load=%d limits=%d..%d",
                 _config.id, raw_pos, angle, direction, current_abs, load_abs, angle_limit.x,
                 angle_limit.y);
    }

    void check_mode(Mode targetMode) {
        if (targetMode == _current_mode) {
            return;
        }

        _scs_bus.SwitchMode(_config.id, static_cast<uint8_t>(targetMode));
        _current_mode = targetMode;
    }
};

void Hal::servo_init() {
    ESP_LOGI("K151Servo", "Initialize calibrated SCS servos");

    if (!servo_available_)
        return;
    if (!_scs_bus.begin(UART_NUM_1, 1000000, K151_SERVO_TX_GPIO, K151_SERVO_RX_GPIO) ||
        _scs_bus.Ping(1) < 0 || _scs_bus.Ping(2) < 0) {
        servo_available_ = false;
        setServoPowerEnabled(false);
        ESP_LOGE("K151Servo", "SCS bus/servo unavailable; supply disabled");
        return;
    }

    ServoConfig_t yaw_servo_config;
    yaw_servo_config.id = 1;
    yaw_servo_config.defaultZeroPos = 460;
    yaw_servo_config.angleLimit = Vector2i(-1280, 1280);
    yaw_servo_config.rawPosLimit = Vector2i(0, 1000);
    yaw_servo_config.settingNs = "servo";
    yaw_servo_config.settingZeroPositionKey = "zero_pos_1";
    yaw_servo_config.enablePwmMode = false;  // Continuous rotation is not exposed to cloud tools.

    ServoConfig_t pitch_servo_config;
    pitch_servo_config.id = 2;
    pitch_servo_config.defaultZeroPos = 620;
    pitch_servo_config.angleLimit = Vector2i(50, 850);
    pitch_servo_config.rawPosLimit = Vector2i(0, 1000);
    pitch_servo_config.settingNs = "servo";
    pitch_servo_config.settingZeroPositionKey = "zero_pos_2";
    pitch_servo_config.enableStallProtection = true;

    auto yaw_servo = std::make_unique<ScsServo>(yaw_servo_config);
    auto pitch_servo = std::make_unique<ScsServo>(pitch_servo_config);
    auto motion = std::make_unique<Motion>(std::move(yaw_servo), std::move(pitch_servo));
    motion->init();

    GetStackChan().attachMotion(std::move(motion));
}
