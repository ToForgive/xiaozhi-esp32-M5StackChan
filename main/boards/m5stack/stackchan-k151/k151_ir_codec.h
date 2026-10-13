// SPDX-License-Identifier: MIT
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace k151 {
inline bool Near(uint16_t sample, int expected) {
    return sample >= expected * 7 / 10 && sample <= expected * 13 / 10;
}
inline std::array<uint16_t, 67> NecDurations(uint8_t address, uint8_t command) {
    std::array<uint16_t, 67> raw{};
    raw[0] = 9000;
    raw[1] = 4500;
    uint32_t word = uint32_t(address) | (uint32_t(uint8_t(~address)) << 8) |
                    (uint32_t(command) << 16) | (uint32_t(uint8_t(~command)) << 24);
    for (int bit = 0; bit < 32; ++bit) {
        raw[2 + bit * 2] = 560;
        raw[3 + bit * 2] = word & (1u << bit) ? 1690 : 560;
    }
    raw[66] = 560;
    return raw;
}
inline bool DecodeNec(const uint16_t* raw, size_t count, uint8_t& address, uint8_t& command) {
    if (count < 67 || !Near(raw[0], 9000) || !Near(raw[1], 4500) || !Near(raw[66], 560))
        return false;
    uint32_t word = 0;
    for (int bit = 0; bit < 32; ++bit) {
        if (!Near(raw[2 + bit * 2], 560))
            return false;
        auto space = raw[3 + bit * 2];
        if (Near(space, 1690))
            word |= 1u << bit;
        else if (!Near(space, 560))
            return false;
    }
    address = word & 255;
    command = (word >> 16) & 255;
    return uint8_t(word >> 8) == uint8_t(~address) && uint8_t(word >> 24) == uint8_t(~command);
}
inline bool IsNecRepeat(const uint16_t* raw, size_t count) {
    return count == 3 && Near(raw[0], 9000) && Near(raw[1], 2250) && Near(raw[2], 560);
}
inline bool ValidateRaw(const uint16_t* raw, size_t count) {
    if (!raw || count < 3 || count > 256)
        return false;
    uint32_t total = 0;
    for (size_t i = 0; i < count; ++i) {
        if (raw[i] < 50 || raw[i] > 30000)
            return false;
        total += raw[i];
    }
    return total <= 300000;  // A bounded frame, maximum 300ms.
}
}  // namespace k151
