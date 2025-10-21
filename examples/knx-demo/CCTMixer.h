#pragma once
#include <stdint.h>

struct WWCW {
  uint8_t warmOut;  // 0..255
  uint8_t coldOut;  // 0..255
};

/**
 * Integer-only CCTMixer.
 * - Input: kelvin (uint32_t), brightness W_percent (0..100, uint8_t).
 * - Output: warm/cold as 0..255 (uint8_t).
 * - Normalized so the stronger channel always reaches W%,
 *   weaker is proportionally less.
 */
class CCTMixer {
public:
  CCTMixer(uint32_t warmK, uint32_t neutralK, uint32_t coldK);

  void setTemperatures(uint32_t warmK, uint32_t neutralK, uint32_t coldK);

  uint32_t warmK()    const { return warmK_; }
  uint32_t neutralK() const { return neutralK_; }
  uint32_t coldK()    const { return coldK_; }

  WWCW compute(uint32_t kelvin, uint8_t W_percent) const;

private:
  uint32_t warmK_;
  uint32_t neutralK_;
  uint32_t coldK_;

  uint32_t mWarm_;
  uint32_t mNeut_;
  uint32_t mCold_;

  void normalize_();
  void recomputeMireds_();
};
