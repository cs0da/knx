// #include "Helper.h"

// namespace Helper {
//     uint8_t pctToByte(uint8_t pct) 
//     {
//         if (pct > 100) pct = 100;
//         // scale 0..100 → 0..255 with rounding
//         return (uint8_t)(((uint16_t)pct * 255u + 50u) / 100u);
//     }

//     uint8_t byteToPct(uint8_t v) 
//     {
//         // scale 0..255 → 0..100 with rounding
//         return (uint8_t)(((uint16_t)v * 100u + 127u) / 255u);
//     }
// }