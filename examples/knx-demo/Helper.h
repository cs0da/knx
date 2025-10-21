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