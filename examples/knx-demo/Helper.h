#pragma once
#include <Arduino.h>

/**
 * @brief Convert between 0–100% and 0–255 values with rounding.
 *
 * These helpers are designed for LED brightness scaling, where input/output
 * may be in percentages (0–100) or 8-bit device values (0–255).
 *
 * Safe for use on 8-bit and 32-bit MCUs: intermediate math is widened to uint16_t
 * to avoid overflow.
 */

namespace Helper
{
    enum DimmingDirection {
        None = 0,
        Up,
        Down
    };

    union DimmingPacket {
    uint8_t raw;
    struct {
        uint8_t data    : 3; // bits 0–2 (1–3)
        uint8_t control : 1; // bit 3 (4)
        uint8_t unused  : 4;
    } bits;
    };

    static inline uint8_t applyPercent_u8(uint8_t value, uint8_t percent) {
        if (percent >= 100) return value;   // handles 100..255 too
        // (value * percent + 50) / 100  -> rounded
        return (uint8_t)(((uint16_t)value * (uint16_t)percent + 50u) / 100u);
    }

    // /**
    //  * @param pct  Percentage value (0..100). Values >100 are clamped.
    //  * @return     Byte value (0..255).
    //  */
    // uint8_t pctToByte(uint8_t pct);

    // /**
    //  * @param v  Byte value (0..255).
    //  * @return   Percentage value (0..100).
    //  */
    // uint8_t byteToPct(uint8_t v);

    inline uint8_t pctToByte(uint8_t pct) 
    {
        if (pct > 100) pct = 100;
        // scale 0..100 → 0..255 with rounding
        return (uint8_t)(((uint16_t)pct * 255u + 50u) / 100u);
    }

    inline uint8_t byteToPct(uint8_t v) 
    {
        // scale 0..255 → 0..100 with rounding
        return (uint8_t)(((uint16_t)v * 100u + 127u) / 255u);
    }
}