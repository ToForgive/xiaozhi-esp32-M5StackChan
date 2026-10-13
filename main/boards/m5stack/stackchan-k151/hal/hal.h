// SPDX-License-Identifier: MIT
#pragma once
#include <driver/i2c_master.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include "drivers/PCF8563_Class/PCF8563_Class.hpp"
#include "drivers/PY32IOExpander_Class/PY32IOExpander_Class.hpp"
#include "drivers/Si12T/Si12T.h"
#include "drivers/bmi270/bmi270.h"
#include "k151_sensor_cache.h"

struct K151Config {
    uint32_t idleShutdownTimeSeconds = 0;  // New devices default to no automatic power-off.
    bool allowShutdownWhenCharging = false;
    uint8_t idleRandomMovementLevel = 2;
};
struct K151Command {
    enum Type { Move, Home, Power, Rgb } type;
    int a = 0, b = 0, c = 0;
};
class Hal {
public:
    enum Event : uint32_t { Pet = 1, Release = 2, Shake = 4, Press = 8 };
    uint32_t millis() { return esp_timer_get_time() / 1000; }
    K151Config getConfig();
    void init(i2c_master_bus_handle_t bus);
    void pollSensors();  // Sensor task; only posts bits, never mutates LVGL/robot.
    uint32_t takeEvents() { return events_.exchange(0); }
    void postEvent(Event event) { events_.fetch_or(event); }
    void registerTools();
    void updateMotion();  // Render worker, called outside the LVGL lock.
    void setRgbColor(uint8_t index, uint8_t r, uint8_t g, uint8_t b);
    void refreshRgb();
    void showRgbColor(uint8_t r, uint8_t g, uint8_t b);
    bool queueCommand(const K151Command& command);
    void requestServoPower(bool enabled) { queueCommand({K151Command::Power, enabled}); }
    bool DisableServoSupplyForShutdown();
    void requestSleep(bool sleeping) { sleep_requested_.store(sleeping ? 1 : 0); }
    bool servoAvailable() const { return servo_available_; }
    bool headTouchAvailable() const { return touch_ != nullptr; }
    bool imuAvailable() const { return bool(imu_); }
    bool rtcAvailable() const { return bool(rtc_); }
    bool inaAvailable() const { return ina_ != nullptr; }
    bool ReadPower(float& volts, float& amps);
    bool ReadRtc(std::string& calendar);

private:
    void servo_init();
    void setServoPowerEnabled(bool enabled);
    bool ReadPowerDevice(float& volts, float& amps);
    struct PowerSample {
        float volts, amps;
    };
    k151::SensorCache<PowerSample> power_cache_;
    k151::SensorCache<std::string> rtc_cache_;
    uint64_t telemetry_tick_ = 0;
    std::unique_ptr<m5::PY32IOExpander_Class> ioe_;
    std::unique_ptr<m5::PCF8563_Class> rtc_;
    std::unique_ptr<BMI270> imu_;
    si12t_handle_t touch_ = nullptr;
    i2c_master_dev_handle_t ina_ = nullptr;
    QueueHandle_t commands_ = nullptr;
    std::mutex ioe_mutex_;
    std::atomic<uint32_t> events_{0};
    std::atomic<int> sleep_requested_{-1};
    std::atomic<bool> shutting_down_{false};
    std::atomic<int> supply_status_{0};  // 0 unknown, 1 off write ACK, 2 on write ACK.
    bool sleep_pending_ = false;
    uint64_t sleep_deadline_ = 0;
    uint32_t servo_ready_at_ = 0;
    bool servo_available_ = false;
    bool servo_power_ = false;
    bool touched_ = false, swiping_ = false;
    int initial_touch_ = 0;
    uint32_t last_imu_ = 0;
};
inline Hal& GetHAL() {
    static Hal hal;
    return hal;
}
