#include "CCTMixer.h"

// ---- helpers ----
static inline void swap_u32(uint32_t &a, uint32_t &b) { uint32_t t=a; a=b; b=t; }
static inline uint32_t clampu32(uint32_t x, uint32_t lo, uint32_t hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}
static inline uint8_t clampu8(int x) {
  if (x < 0) return 0;
  if (x > 255) return 255;
  return (uint8_t)x;
}

// ---- ctor / config ----
CCTMixer::CCTMixer(uint32_t warmK, uint32_t neutralK, uint32_t coldK)
: warmK_(warmK), neutralK_(neutralK), coldK_(coldK),
  mWarm_(0), mNeut_(0), mCold_(0)
{
  normalize_();
  recomputeMireds_();
}

void CCTMixer::setTemperatures(uint32_t warmK, uint32_t neutralK, uint32_t coldK) {
  warmK_ = warmK;
  neutralK_ = neutralK;
  coldK_ = coldK;
  normalize_();
  recomputeMireds_();
}

void CCTMixer::normalize_() {
  if (warmK_ > coldK_) swap_u32(warmK_, coldK_);
  if (neutralK_ < warmK_) neutralK_ = warmK_;
  if (neutralK_ > coldK_) neutralK_ = coldK_;
}

void CCTMixer::recomputeMireds_() {
  mWarm_ = warmK_    ? 1000000UL / warmK_    : 0;
  mNeut_ = neutralK_ ? 1000000UL / neutralK_ : 0;
  mCold_ = coldK_    ? 1000000UL / coldK_    : 0;
}

// ---- main compute ----
WWCW CCTMixer::compute(uint32_t kelvin, uint8_t W_percent) const {
  if (W_percent == 0) return { 0, 0 };

  // Endpoints
  if (kelvin <= warmK_) return { (uint8_t)((W_percent * 255 + 50) / 100), 0 };
  if (kelvin >= coldK_) return { 0, (uint8_t)((W_percent * 255 + 50) / 100) };
  if (kelvin == neutralK_) {
    uint8_t v = (uint8_t)((W_percent * 255 + 50) / 100);
    return { v, v };
  }

  // Clamp and compute mired
  kelvin = clampu32(kelvin, warmK_, coldK_);
  const uint32_t mK = kelvin ? 1000000UL / kelvin : 0;

  // Warm weight t in Q15
  uint32_t tQ15;
  if (mK >= mWarm_) {
    tQ15 = 65535U;
  } else if (mK <= mCold_) {
    tQ15 = 0U;
  } else if (mK >= mNeut_) {
    uint32_t num = mK - mNeut_;
    uint32_t den = mWarm_ - mNeut_;
    tQ15 = 32768U + (uint32_t)(((uint64_t)num * 32767ULL + (den/2)) / den);
  } else {
    uint32_t num = mK - mCold_;
    uint32_t den = mNeut_ - mCold_;
    tQ15 = (uint32_t)(((uint64_t)num * 32767ULL + (den/2)) / den);
  }

  // Linear mix
  uint32_t ww_lin = tQ15;
  uint32_t cw_lin = 65535U - tQ15;
  uint32_t maxc   = (ww_lin > cw_lin) ? ww_lin : cw_lin;

  if (maxc == 0) return { 0, 0 };

  // Normalize & scale with brightness
  uint32_t ww = ((uint64_t)W_percent * 255ULL * ww_lin + (maxc/2)) / (100ULL * maxc);
  uint32_t cw = ((uint64_t)W_percent * 255ULL * cw_lin + (maxc/2)) / (100ULL * maxc);

  return { clampu8((int)ww), clampu8((int)cw) };
}
