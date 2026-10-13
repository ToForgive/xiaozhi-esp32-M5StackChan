// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <string_view>
#include "device_state.h"
#include "stackchan/avatar/avatar/elements/emotion.h"

namespace k151 {
using Emotion = stackchan::avatar::Emotion;
enum class Decoration { None, Heart, Angry, Sweat, Shy, Dizzy };
struct Expression {
    Emotion emotion;
    Decoration decoration;
};

// Explicit aliases; doubtful is the original spelling, thinking/confused are newer keys.
inline Expression MapEmotion(std::string_view key) {
    if (key == "happy" || key == "laughing" || key == "funny" || key == "delicious")
        return {Emotion::Happy, Decoration::None};
    if (key == "loving")
        return {Emotion::Happy, Decoration::Heart};
    if (key == "angry")
        return {Emotion::Angry, Decoration::Angry};
    if (key == "sad" || key == "crying")
        return {Emotion::Sad, Decoration::None};
    if (key == "sleepy")
        return {Emotion::Sleepy, Decoration::None};
    if (key == "doubtful" || key == "thinking" || key == "confused")
        return {Emotion::Doubt, Decoration::None};
    if (key == "shocked" || key == "surprised" || key == "scared" || key == "embarrassed")
        return {Emotion::Doubt, Decoration::Sweat};
    if (key == "shy")
        return {Emotion::Happy, Decoration::Shy};
    if (key == "silly")
        return {Emotion::Happy, Decoration::Dizzy};
    // neutral, cool, confident, relaxed, winking, kissy, link and unknown keys.
    return {Emotion::Neutral, Decoration::None};
}

enum class Reaction { None, Pet, Shake };
// Pure state owner, independent of LVGL/RTOS. Temporary reactions never capture stale base state.
class Model {
public:
    void SetExpression(Expression expression) { base_ = expression; }
    void SetState(DeviceState state) { state_ = state; }
    void SetSleep(bool sleep) {
        sleep_ = sleep;
        if (sleep)
            reaction_ = Reaction::None;
    }
    void Pet(uint64_t now) {
        if (sleep_ || reaction_ == Reaction::Shake || !Interactive())
            return;
        reaction_ = Reaction::Pet;
        // Preserve held-pet feedback; bound recovery if the release event is lost.
        restore_at_ = now + 30000;
    }
    void Release(uint64_t now) {
        if (reaction_ == Reaction::Pet) {
            restore_at_ = now + 3000;
        }
    }
    void Shake(uint64_t now) {
        if (sleep_ || !Interactive())
            return;
        reaction_ = Reaction::Shake;
        restore_at_ = now + 4000;
    }
    void Tick(uint64_t now) {
        if (!Interactive() || (reaction_ != Reaction::None && now >= restore_at_))
            reaction_ = Reaction::None;
    }
    Expression Visible() const {
        if (sleep_)
            return {Emotion::Sleepy, Decoration::None};
        if (reaction_ == Reaction::Pet)
            return {Emotion::Happy, Decoration::Heart};
        if (reaction_ == Reaction::Shake)
            return {base_.emotion, Decoration::Dizzy};
        return base_;
    }
    Reaction GetReaction() const { return reaction_; }
    bool Speaking() const {
        return !sleep_ && state_ == kDeviceStateSpeaking && reaction_ != Reaction::Shake;
    }
    bool Idle() const {
        return !sleep_ && base_.emotion != Emotion::Sleepy && state_ == kDeviceStateIdle &&
               reaction_ == Reaction::None;
    }
    bool Sleeping() const { return sleep_; }
    bool Interactive() const {
        return state_ == kDeviceStateIdle || state_ == kDeviceStateListening ||
               state_ == kDeviceStateSpeaking;
    }

private:
    Expression base_{Emotion::Neutral, Decoration::None};
    DeviceState state_ = kDeviceStateUnknown;
    Reaction reaction_ = Reaction::None;
    uint64_t restore_at_ = 0;
    bool sleep_ = false;
};
}  // namespace k151
