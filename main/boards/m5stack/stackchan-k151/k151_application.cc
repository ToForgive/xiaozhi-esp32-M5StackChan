// SPDX-License-Identifier: MIT
#include "k151_application.h"
#include "application.h"
DeviceState K151DeviceState() { return Application::GetInstance().GetDeviceState(); }
void K151ToggleChat() {
    Application::GetInstance().Schedule([]() {
        auto& app = Application::GetInstance();
        auto state = app.GetDeviceState();
        if (state == kDeviceStateIdle || state == kDeviceStateListening ||
            state == kDeviceStateSpeaking)
            app.ToggleChatState();
    });
}
