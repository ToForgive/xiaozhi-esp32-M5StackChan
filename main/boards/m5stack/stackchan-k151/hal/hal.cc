// SPDX-License-Identifier: MIT
#include "hal.h"
#include <driver/gpio.h>
#include <esp_log.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include "k151_ir.h"
#include "mcp_server.h"
#include "motion_detector.h"
#include "settings.h"
#include "stackchan/stackchan.h"

K151Config Hal::getConfig() {
    Settings settings("xiaozhi", false);
    K151Config config;
    config.idleShutdownTimeSeconds = std::clamp<int32_t>(settings.GetInt("idle_sec", 0), 0, 86400);
    config.allowShutdownWhenCharging = settings.GetBool("ext_pwr", false);
    config.idleRandomMovementLevel = std::clamp<int32_t>(settings.GetInt("idle_lv", 2), 0, 3);
    return config;
}

void Hal::init(i2c_master_bus_handle_t bus) {
    commands_ = xQueueCreate(8, sizeof(K151Command));
    ESP_ERROR_CHECK(commands_ ? ESP_OK : ESP_ERR_NO_MEM);
    uitk::ui_hal::on_get_tick([this]() { return millis(); });
    uitk::ui_hal::on_delay([](uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); });
    // GPIO2 belongs to Grove A on the public schematics; leave it available to extensions.
    ioe_ = std::make_unique<m5::PY32IOExpander_Class>(bus);
    bool ready = false;
    for (int attempt = 0; attempt < 6; ++attempt) {
        vTaskDelay(pdMS_TO_TICKS(200));
        if (ioe_->begin()) {
            ready = true;
            break;
        }
    }
    if (ready) {
        ioe_->setDirection(0, true);
        ioe_->setPullMode(0, true);
        servo_power_ = servo_available_ = ioe_->tryDigitalWrite(0, true) == ESP_OK;
        supply_status_.store(servo_power_ ? 2 : 0);
        ioe_->setDirection(13, true);
        ioe_->setPullMode(13, true);
        ioe_->setDriveMode(13, false);
        ioe_->setLedCount(12);
        showRgbColor(0, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(200));
    } else {
        ioe_.reset();
        ESP_LOGW("K151", "PY32 unavailable; servo supply and RGB disabled");
    }
    servo_init();
    GetStackChan().motion().setAutoTorqueReleaseEnabled(true);
    GetStackChan().motion().setAutoAngleSyncEnabled(true);
    si12t_config_t touch_config{.i2c_bus = bus, .dev_addr = SI12T_GND_ADDRESS};
    if (i2c_master_probe(bus, SI12T_GND_ADDRESS, 40) == ESP_OK &&
        si12t_init(&touch_config, &touch_) == ESP_OK) {
        if (si12t_setup(touch_, SI12T_TYPE_LOW, SI12T_SENSITIVITY_LEVEL_3) != ESP_OK) {
            si12t_delete(touch_);
            touch_ = nullptr;
        }
    }
    if (i2c_master_probe(bus, 0x69, 40) == ESP_OK) {
        imu_ = std::make_unique<BMI270>(bus, 0x69);
        if (!imu_->begin())
            imu_.reset();
    }
    if (i2c_master_probe(bus, 0x51, 40) == ESP_OK) {
        rtc_ = std::make_unique<m5::PCF8563_Class>(bus);
        if (!rtc_->begin())
            rtc_.reset();
    }
    if (i2c_master_probe(bus, 0x41, 40) == ESP_OK) {
        i2c_device_config_t config{
            .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = 0x41, .scl_speed_hz = 100000};
        if (i2c_master_bus_add_device(bus, &config, &ina_) == ESP_OK) {
            // INA226 current LSB 250uA, calibration = 0.00512/(0.00025*0.01)=2048.
            uint8_t calibration[] = {0x05, 0x08, 0x00};
            if (i2c_master_transmit(ina_, calibration, sizeof(calibration), 40) != ESP_OK) {
                i2c_master_bus_rm_device(ina_);
                ina_ = nullptr;
            }
        }
    }
    GetK151Ir().Init();
}
void Hal::pollSensors() {
    if (touch_) {
        uint8_t result = 0, intensity[3]{};
        // Failed reads must not turn stale data into a press or release event.
        if (si12t_read_touch_result(touch_, &result) == ESP_OK) {
            si12t_parse_touch_result_to(result, intensity);
            int sum = intensity[0] + intensity[1] + intensity[2];
            int position = sum ? (100 * (intensity[2] - intensity[0])) / sum : 0;
            if (sum && !touched_) {
                initial_touch_ = position;
                touched_ = true;
                swiping_ = false;
                postEvent(Press);
            } else if (sum && !swiping_ && std::abs(position - initial_touch_) > 40) {
                swiping_ = true;
                postEvent(Pet);
            } else if (!sum && touched_) {
                touched_ = swiping_ = false;
                postEvent(Release);
            }
        }
    }
    static MotionDetector detector;
    detector.setShakeThreshold(16.0f);
    const auto now = millis();
    if (imu_ && now - last_imu_ >= 100) {
        last_imu_ = now;
        if (imu_->update()) {
            const auto& data = imu_->getData();
            detector.update(data.accel_x, data.accel_y, data.accel_z);
            if (detector.isShakeDetected())
                postEvent(Shake);
        }
    }
    const uint64_t now64 = esp_timer_get_time() / 1000;
    if (now64 - telemetry_tick_ >= 1000) {
        telemetry_tick_ = now64;
        float volts, amps;
        if (ReadPowerDevice(volts, amps))
            power_cache_.Update({volts, amps}, now64);
        if (rtc_) {
            m5::rtc_date_t date;
            m5::rtc_time_t time;
            if (rtc_->getDateTime(&date, &time) && date.year >= 2000 && date.month >= 1 &&
                date.month <= 12 && date.date >= 1 && date.date <= 31 && time.hours >= 0 &&
                time.hours < 24 && time.minutes >= 0 && time.minutes < 60 && time.seconds >= 0 &&
                time.seconds < 60) {
                char buffer[32];
                snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d", date.year,
                         date.month, date.date, time.hours, time.minutes, time.seconds);
                rtc_cache_.Update(std::string(buffer), now64);
            } else {
                rtc_cache_.Invalidate();
            }
        }
    }
}
bool Hal::queueCommand(const K151Command& command) {
    return commands_ && xQueueSend(commands_, &command, 0) == pdTRUE;
}
void Hal::updateMotion() {
    K151Command command;
    auto& motion = GetStackChan().motion();
    if (shutting_down_.load()) {
        motion.stop();
        if (servo_power_)
            motion.setTorqueEnabled(false);
        servo_power_ = false;
        return;
    }
    int sleep = sleep_requested_.exchange(-1);
    if (sleep >= 0) {
        sleep_pending_ = sleep;
        motion.setModifyLock(sleep);
        if (sleep) {
            motion.goHome(80);
            sleep_deadline_ = esp_timer_get_time() / 1000 + 5000;
        } else {
            setServoPowerEnabled(true);
        }
    }
    // Bound work per animation frame. Tools report queue acceptance, not physical completion.
    for (int count = 0; count < 2 && xQueueReceive(commands_, &command, 0) == pdTRUE; ++count) {
        switch (command.type) {
            case K151Command::Move:
                if (servo_power_ && !motion.isModifyLocked())
                    motion.moveWithSpeed(command.a, command.b, command.c);
                break;
            case K151Command::Home:
                if (servo_power_ && !sleep_pending_)
                    motion.goHome(200);
                break;
            case K151Command::Power:
                if (command.a && sleep_pending_)
                    break;
                if (!command.a)
                    sleep_pending_ = false;
                setServoPowerEnabled(command.a);
                break;
            case K151Command::Rgb:
                showRgbColor(command.a, command.b, command.c);
                break;
        }
    }
    if (servo_power_ && static_cast<int32_t>(millis() - servo_ready_at_) >= 0)
        motion.update();
    if (sleep_pending_ &&
        (!motion.isMoving() || uint64_t(esp_timer_get_time() / 1000) >= sleep_deadline_)) {
        setServoPowerEnabled(false);
        sleep_pending_ = false;
        // Keep movement blocked until an explicit wake request.
        motion.setModifyLock(true);
    }
}
bool Hal::DisableServoSupplyForShutdown() {
    // A shutdown request must not be lost behind a full control queue.
    shutting_down_.store(true);
    if (!ioe_)
        return false;
    std::lock_guard lock(ioe_mutex_);
    const bool ack = ioe_->tryDigitalWrite(0, false) == ESP_OK;
    supply_status_.store(ack ? 1 : 0);
    return ack;
}
void Hal::setServoPowerEnabled(bool enabled) {
    if (!ioe_ || (enabled && !servo_available_))
        return;
    if (!enabled) {
        auto& motion = GetStackChan().motion();
        motion.stop();
        motion.setTorqueEnabled(false);
    }
    std::lock_guard lock(ioe_mutex_);
    if (enabled && shutting_down_.load())
        return;
    const bool ack = ioe_->tryDigitalWrite(0, enabled) == ESP_OK;
    supply_status_.store(ack ? (enabled ? 2 : 1) : 0);
    // An I/O failure never permits motion or claims that the physical supply is off.
    servo_power_ = enabled && ack;
    if (!ack) {
        GetStackChan().motion().stop();
        GetStackChan().motion().setTorqueEnabled(false);
        ESP_LOGE("K151", "VM write failed; physical supply unknown, motion inhibited");
    }
    if (enabled)
        servo_ready_at_ = millis() + 200;
}
void Hal::setRgbColor(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    if (!ioe_ || index >= 12)
        return;
    std::lock_guard lock(ioe_mutex_);
    ioe_->setLedColor(index, r, g, b);
}
void Hal::refreshRgb() {
    if (!ioe_)
        return;
    std::lock_guard lock(ioe_mutex_);
    ioe_->refreshLeds();
}
void Hal::showRgbColor(uint8_t r, uint8_t g, uint8_t b) {
    if (!ioe_)
        return;
    std::lock_guard lock(ioe_mutex_);
    for (int index = 0; index < 12; ++index)
        ioe_->setLedColor(index, r, g, b);
    ioe_->refreshLeds();
}
bool Hal::ReadPower(float& volts, float& amps) {
    auto sample = power_cache_.Read(esp_timer_get_time() / 1000, 5000);
    if (!sample)
        return false;
    volts = sample->volts;
    amps = sample->amps;
    return true;
}
bool Hal::ReadRtc(std::string& calendar) {
    auto sample = rtc_cache_.Read(esp_timer_get_time() / 1000, 5000);
    if (!sample)
        return false;
    calendar = *sample;
    return true;
}
bool Hal::ReadPowerDevice(float& volts, float& amps) {
    if (!ina_)
        return false;
    uint8_t bus_reg = 0x02, current_reg = 0x04, raw_bus[2], raw_current[2];
    if (i2c_master_transmit_receive(ina_, &bus_reg, 1, raw_bus, 2, 40) != ESP_OK ||
        i2c_master_transmit_receive(ina_, &current_reg, 1, raw_current, 2, 40) != ESP_OK)
        return false;
    volts = ((raw_bus[0] << 8) | raw_bus[1]) * 0.00125f;
    amps = static_cast<int16_t>((raw_current[0] << 8) | raw_current[1]) * 0.00025f;
    return true;
}
void Hal::registerTools() {
    auto& server = McpServer::GetInstance();
    server.AddTool("self.robot.get_capabilities",
                   "Read K151 hardware availability; no account or model API key is needed.", {},
                   [this](const PropertyList&) -> ReturnValue {
                       auto json = cJSON_CreateObject();
                       cJSON_AddBoolToObject(json, "servo", servo_available_);
                       cJSON_AddStringToObject(json, "servo_supply_command",
                                               supply_status_.load() == 2   ? "on_write_acked"
                                               : supply_status_.load() == 1 ? "off_write_acked"
                                                                            : "unknown_io_error");
                       cJSON_AddBoolToObject(json, "shutdown_latched", shutting_down_.load());
                       cJSON_AddBoolToObject(json, "rgb", bool(ioe_));
                       cJSON_AddBoolToObject(json, "head_touch", headTouchAvailable());
                       cJSON_AddBoolToObject(json, "imu", imuAvailable());
                       cJSON_AddBoolToObject(json, "rtc", rtcAvailable());
                       cJSON_AddBoolToObject(json, "ina226", inaAvailable());
                       cJSON_AddBoolToObject(json, "ir", GetK151Ir().Available());
                       cJSON_AddBoolToObject(json, "nfc", false);
                       cJSON_AddStringToObject(json, "not_implemented",
                                               "ST25R3916 NFC, BMM150, LTR553, microSD storage");
                       cJSON_AddStringToObject(json, "stall_protection",
                                               "pitch feedback only; no yaw current protection");
                       return json;
                   });
    if (servo_available_) {
        server.AddTool("self.robot.move",
                       "Queue bounded head pose. Angles are tenths of a degree; speed is a spring "
                       "response parameter. No continuous rotation.",
                       PropertyList({Property("yaw", kPropertyTypeInteger, -1280, 1280),
                                     Property("pitch", kPropertyTypeInteger, 50, 850),
                                     Property("speed", kPropertyTypeInteger, 200, 1, 500)}),
                       [this](const PropertyList& p) -> ReturnValue {
                           return queueCommand({K151Command::Move, p["yaw"].value<int>(),
                                                p["pitch"].value<int>(), p["speed"].value<int>()});
                       });
        server.AddTool("self.robot.home",
                       "Queue calibrated home pose (pitch clamps to the safe minimum).", {},
                       [this](const PropertyList&) -> ReturnValue {
                           return queueCommand({K151Command::Home});
                       });
        server.AddTool(
            "self.robot.servo_power",
            "Queue servo supply enable/disable; disabling stops motion and releases torque.",
            PropertyList({Property("enabled", kPropertyTypeBoolean)}),
            [this](const PropertyList& p) -> ReturnValue {
                return queueCommand({K151Command::Power, p["enabled"].value<bool>()});
            });
    }
    if (ioe_)
        server.AddTool("self.robot.rgb", "Queue color for all 12 robot LEDs.",
                       PropertyList({Property("red", kPropertyTypeInteger, 0, 255),
                                     Property("green", kPropertyTypeInteger, 0, 255),
                                     Property("blue", kPropertyTypeInteger, 0, 255)}),
                       [this](const PropertyList& p) -> ReturnValue {
                           return queueCommand({K151Command::Rgb, p["red"].value<int>(),
                                                p["green"].value<int>(), p["blue"].value<int>()});
                       });
    if (ina_)
        server.AddTool("self.robot.power_read",
                       "Read INA226 bus volts/current amps, positive discharge. AXP2101 supplies "
                       "battery percentage.",
                       {}, [this](const PropertyList&) -> ReturnValue {
                           float volts, amps;
                           if (!ReadPower(volts, amps))
                               return std::string("INA226 snapshot unavailable or stale");
                           auto json = cJSON_CreateObject();
                           cJSON_AddNumberToObject(json, "volts", volts);
                           cJSON_AddNumberToObject(json, "amps", amps);
                           return json;
                       });
    if (rtc_)
        server.AddTool("self.robot.rtc_read",
                       "Read the hardware RTC calendar without changing system/server time. Clock "
                       "validity must be checked on hardware.",
                       {}, [this](const PropertyList&) -> ReturnValue {
                           std::string calendar;
                           if (!ReadRtc(calendar))
                               return std::string("RTC snapshot unavailable, invalid or stale");
                           return calendar;
                       });
    GetK151Ir().RegisterTools();
}
