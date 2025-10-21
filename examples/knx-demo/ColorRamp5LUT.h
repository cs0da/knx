#pragma once
#include <stdint.h>
#include <math.h>

class ColorRamp5LUT {
public:
  struct Channels { uint8_t r, g, b, ww, w; };
  struct Calib    { uint8_t minOut; uint8_t maxOut; };
  struct Flags    { bool r, g, b, ww, w; };

  // ---- Constructors ----
  explicit ColorRamp5LUT(unsigned long fullDurationMs,
                         float gammaAll = 2.2f,
                         Calib calibAll = {0,255});

  ColorRamp5LUT(unsigned long fullDurationMs,
                float gammaAll,
                Calib calibR, Calib calibG, Calib calibB, Calib calibWW, Calib calibW);

  ColorRamp5LUT(unsigned long fullDurationMs,
                float gR, float gG, float gB, float gWW, float gW,
                Calib calibAll);

  ColorRamp5LUT(unsigned long fullDurationMs,
                float gR, float gG, float gB, float gWW, float gW,
                Calib calibR, Calib calibG, Calib calibB, Calib calibWW, Calib calibW);

  // ---- Main control ----
  void start(const Channels& current, const Channels& target, unsigned long nowMs);
  void retargetFromCurrent(const Channels& newTarget, unsigned long nowMs);
  void retargetOnlyW(uint8_t newW, unsigned long nowMs);

  // ---- Sampling ----
  Channels get(unsigned long nowMs);
  Channels getWithFlags(unsigned long nowMs, Flags& changed);

  // ---- Configuration ----
  void setGammaAll(float g);
  void setGammas(float gR, float gG, float gB, float gWW, float gW);
  void setCalibAll(uint8_t minOut, uint8_t maxOut);
  void setCalibPerChannel(Calib r, Calib g, Calib b, Calib ww, Calib w);
  void setChangeThreshold(uint8_t eps) { changeThreshold = eps; }

  bool isActive() const { return active; }

private:
  struct GammaBase {
    const uint8_t* dev2lin;
    const uint8_t* lin2dev;
    float gamma;
  };

  struct ChanLUT {
    uint8_t toLinearCal[256];
    uint8_t fromLinearFloored[256];
    Calib   calib{0,255};
    float   gamma{2.2f};
  };

  static inline uint8_t clamp8(int v) {
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
  }

  static inline uint8_t lerp8(uint8_t a, uint8_t b, float t) {
    int r = (int)((float)a + ((float)b - (float)a) * t + 0.5f);
    return clamp8(r);
  }

  static GammaBase getGammaBase(float gamma);
  static void buildChannelLUT_fast(ChanLUT& L, const GammaBase& base);
  static inline uint8_t rescale_0_255_to_min_max(uint8_t in, uint8_t minOut, uint8_t maxOut);

  float maxPerceptualDistance(const Channels& a, const Channels& b) const;
  Channels computeOut(unsigned long nowMs) const;

  unsigned long fullDurationMs{0}, durationMs{0}, startTimeMs{0};
  bool active{false};

  Channels startLog{0,0,0,0,0};
  Channels targetLog{0,0,0,0,0};

  ChanLUT R{}, G{}, B{}, WW{}, W{};
  Channels prevOut{0,0,0,0,0};
  bool prevValid{false};
  uint8_t changeThreshold{0};
};
