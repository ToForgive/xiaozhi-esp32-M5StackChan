// SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
// SPDX-License-Identifier: MIT
#pragma once
#include <memory>
#include "modifiable.h"

namespace stackchan {
// Board-scoped robot owner. The K151 render worker owns all mutation.
class StackChan : public Modifiable {
public:
    StackChan() {
        // Inert fallback keeps Avatar usable if the servo supply/bus is absent.
        _motion = std::make_unique<motion::Motion>(std::make_unique<InertServo>(),
                                                   std::make_unique<InertServo>());
    }
    void attachMotion(std::unique_ptr<motion::Motion> value) { _motion = std::move(value); }
    void attachAvatar(std::unique_ptr<avatar::Avatar> value) { _avatar = std::move(value); }
    motion::Motion& motion() override { return *_motion; }
    avatar::Avatar& avatar() override { return *_avatar; }
    bool hasAvatar() override { return bool(_avatar); }
    addon::NeonLight& leftNeonLight() override { return _left; }
    addon::NeonLight& rightNeonLight() override { return _right; }
    int addModifier(std::unique_ptr<Modifier> value) { return _modifiers.create(std::move(value)); }
    Modifier* getModifier(int id) { return _modifiers.get(id); }
    bool removeModifier(int id) { return _modifiers.destroy(id); }
    void updateVisual() {
        _modifiers.forEach([this](Modifier* value, int) { value->_update(*this); });
        _modifiers.cleanup();
        if (_avatar)
            _avatar->update();
    }

private:
    class InertServo : public motion::Servo {
    protected:
        void set_angle_impl(int) override {}
    };
    std::unique_ptr<avatar::Avatar> _avatar;
    std::unique_ptr<motion::Motion> _motion;
    addon::LeftNeonLight _left;
    addon::RightNeonLight _right;
    ObjectPool<Modifier> _modifiers;
};
}  // namespace stackchan
inline stackchan::StackChan& GetStackChan() {
    static stackchan::StackChan value;
    return value;
}
