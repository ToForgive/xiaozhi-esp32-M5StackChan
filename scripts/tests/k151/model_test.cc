#include <cassert>
#include <iostream>
#include "k151_ir_codec.h"
#include "k151_model.h"
using namespace k151;
int main() {
    Model m;
    m.SetState(kDeviceStateSpeaking);
    m.SetExpression(MapEmotion("sad"));
    m.Pet(100);
    assert(m.Visible().emotion == Emotion::Happy && m.Speaking());
    m.SetExpression(MapEmotion("angry"));
    m.Release(300);
    m.Tick(3299);
    assert(m.GetReaction() == Reaction::Pet);
    m.Tick(3300);
    assert(m.Visible().emotion == Emotion::Angry && m.Speaking());
    m.Pet(4000);
    m.Shake(4100);
    assert(m.GetReaction() == Reaction::Shake && !m.Speaking());
    m.Pet(4200);
    assert(m.GetReaction() == Reaction::Shake);
    m.SetExpression(MapEmotion("confused"));
    m.Tick(8100);
    assert(m.Visible().emotion == Emotion::Doubt && m.Speaking());
    m.Shake(9000);
    m.SetState(kDeviceStateListening);
    m.Tick(9001);
    assert(!m.Speaking());  // Interruption must not reopen the mouth.
    m.SetState(kDeviceStateConnecting);
    m.Tick(9002);
    assert(m.GetReaction() == Reaction::None && !m.Idle());
    m.SetState(kDeviceStateIdle);
    assert(m.Idle());
    m.Pet(10000);
    m.SetSleep(true);
    assert(m.Sleeping() && !m.Idle());
    assert(m.Visible().emotion == Emotion::Sleepy && m.GetReaction() == Reaction::None);
    m.SetExpression(MapEmotion("happy"));
    m.SetSleep(false);
    assert(m.Idle() && m.Visible().emotion == Emotion::Happy);
    m.SetState(kDeviceStateUpgrading);
    m.Shake(11000);
    assert(m.GetReaction() == Reaction::None);
    m.SetState(kDeviceStateActivating);
    m.Pet(11000);
    assert(m.GetReaction() == Reaction::None);
    m.SetState(kDeviceStateFatalError);
    m.Pet(11000);
    assert(m.GetReaction() == Reaction::None);
    m.SetState(kDeviceStateSpeaking);
    constexpr uint64_t wrap = uint64_t(UINT32_MAX) + 1;
    m.Shake(wrap - 2000);
    m.Tick(wrap + 1999);
    assert(m.GetReaction() == Reaction::Shake);
    m.Tick(wrap + 2000);
    assert(m.GetReaction() == Reaction::None && m.Speaking());
    assert(MapEmotion("doubtful").emotion == MapEmotion("thinking").emotion);
    assert(MapEmotion("link").emotion == Emotion::Neutral);
    assert(MapEmotion("new-unknown-key").emotion == Emotion::Neutral);
    assert(MapEmotion("loving").decoration == Decoration::Heart);
    assert(MapEmotion("shocked").decoration == Decoration::Sweat);
    Model held;
    held.SetState(kDeviceStateIdle);
    held.SetExpression(MapEmotion("sad"));
    held.Pet(100);
    held.Tick(5000);
    assert(held.GetReaction() == Reaction::Pet);  // Hold until release, not just three seconds.
    held.Release(5000);
    held.Tick(8000);
    assert(held.Visible().emotion == Emotion::Sad && held.Idle());
    held.Pet(10000);
    held.Tick(40000);
    assert(held.GetReaction() == Reaction::None);  // Lost release cannot pin the reaction forever.
    held.SetExpression(MapEmotion("sleepy"));
    assert(!held.Idle());
    // Exercise every ordinary NEC address/command byte and complement validation.
    for (unsigned address = 0; address < 256; ++address)
        for (unsigned command = 0; command < 256; ++command) {
            auto raw = NecDurations(address, command);
            uint8_t a, c;
            assert(ValidateRaw(raw.data(), raw.size()));
            assert(DecodeNec(raw.data(), raw.size(), a, c) && a == address && c == command);
        }
    auto raw = NecDurations(0x00, 0xe0);
    uint8_t a, c;
    raw[3] = raw[3] == 560 ? 1690 : 560;
    assert(!DecodeNec(raw.data(), raw.size(), a, c));
    raw = NecDurations(0, 1);
    raw[0] = 200;
    assert(!DecodeNec(raw.data(), raw.size(), a, c));
    const uint16_t repeat[] = {9000, 2250, 560};
    assert(IsNecRepeat(repeat, 3));
    assert(!IsNecRepeat(raw.data(), raw.size()));
    assert(!DecodeNec(raw.data(), 66, a, c));
    assert(!ValidateRaw(nullptr, 3));
    std::array<uint16_t, 257> large{};
    large.fill(560);
    assert(!ValidateRaw(large.data(), 257));
    large.fill(30000);
    assert(!ValidateRaw(large.data(), 20));
    std::cout << "K151 state/recovery and 65536 NEC pairs passed\n";
}
