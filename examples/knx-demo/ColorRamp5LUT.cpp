#include "ColorRamp5LUT.h"

// --------------------------------------------------------------
// Correct 8-bit rescale [0..255] → [minOut..maxOut]
// --------------------------------------------------------------
inline uint8_t ColorRamp5LUT::rescale_0_255_to_min_max(uint8_t in, uint8_t minOut, uint8_t maxOut) {
  if (in == 0) return 0;
  if (minOut > maxOut) { uint8_t t = minOut; minOut = maxOut; maxOut = t; }
  if (minOut == 0 && maxOut == 255) return in; // fast identity

  uint16_t span = (uint16_t)maxOut - (uint16_t)minOut;
  uint16_t inc  = ((uint16_t)in * span + 127) / 255;
  uint16_t out  = (uint16_t)minOut + inc;
  if (out > 255) out = 255;
  return (uint8_t)out;
}

// --------------------------------------------------------------
// Precomputed γ=2.2 base tables
// --------------------------------------------------------------
static const uint8_t dev2lin_22[256] = {
  0,  21,  28,  34,  39,  43,  46,  50,  53,  56,  59,  61,  64,  66,  68,  70,
  72,  74,  76,  78,  80,  82,  84,  85,  87,  89,  90,  92,  93,  95,  96,  98,
  99, 101, 102, 103, 105, 106, 107, 109, 110, 111, 112, 114, 115, 116, 117, 118,
  119, 120, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135,
  136, 137, 138, 139, 140, 141, 142, 143, 144, 144, 145, 146, 147, 148, 149, 150,
  151, 151, 152, 153, 154, 155, 156, 156, 157, 158, 159, 160, 160, 161, 162, 163,
  164, 164, 165, 166, 167, 167, 168, 169, 170, 170, 171, 172, 173, 173, 174, 175,
  175, 176, 177, 178, 178, 179, 180, 180, 181, 182, 182, 183, 184, 184, 185, 186,
  186, 187, 188, 188, 189, 190, 190, 191, 192, 192, 193, 194, 194, 195, 195, 196,
  197, 197, 198, 199, 199, 200, 200, 201, 202, 202, 203, 203, 204, 205, 205, 206,
  206, 207, 207, 208, 209, 209, 210, 210, 211, 212, 212, 213, 213, 214, 214, 215,
  215, 216, 217, 217, 218, 218, 219, 219, 220, 220, 221, 221, 222, 223, 223, 224,
  224, 225, 225, 226, 226, 227, 227, 228, 228, 229, 229, 230, 230, 231, 231, 232,
  232, 233, 233, 234, 234, 235, 235, 236, 236, 237, 237, 238, 238, 239, 239, 240,
  240, 241, 241, 242, 242, 243, 243, 244, 244, 245, 245, 246, 246, 247, 247, 248,
  248, 249, 249, 250, 250, 251, 251, 252, 252, 253, 253, 254, 254, 255, 255, 255
};

static const uint8_t lin2dev_22[256] = {
  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,
  1,   1,   1,   1,   1,   1,   1,   1,   1,   2,   2,   2,   2,   2,   2,   2,
  3,   3,   3,   3,   3,   4,   4,   4,   4,   5,   5,   5,   5,   6,   6,   6,
  6,   7,   7,   7,   8,   8,   8,   9,   9,   9,  10,  10,  11,  11,  11,  12,
  12,  13,  13,  13,  14,  14,  15,  15,  16,  16,  17,  17,  18,  18,  19,  19,
  20,  20,  21,  22,  22,  23,  23,  24,  25,  25,  26,  26,  27,  28,  28,  29,
  30,  30,  31,  32,  33,  33,  34,  35,  35,  36,  37,  38,  39,  39,  40,  41,
  42,  43,  43,  44,  45,  46,  47,  48,  49,  49,  50,  51,  52,  53,  54,  55,
  56,  57,  58,  59,  60,  61,  62,  63,  64,  65,  66,  67,  68,  69,  70,  71,
  73,  74,  75,  76,  77,  78,  79,  81,  82,  83,  84,  85,  87,  88,  89,  90,
  91,  93,  94,  95,  97,  98,  99, 100, 102, 103, 105, 106, 107, 109, 110, 111,
  113, 114, 116, 117, 119, 120, 121, 123, 124, 126, 127, 129, 130, 132, 133, 135,
  137, 138, 140, 141, 143, 145, 146, 148, 149, 151, 153, 154, 156, 158, 159, 161,
  163, 165, 166, 168, 170, 172, 173, 175, 177, 179, 181, 182, 184, 186, 188, 190,
  192, 194, 196, 197, 199, 201, 203, 205, 207, 209, 211, 213, 215, 217, 219, 221,
  223, 225, 227, 229, 231, 234, 236, 238, 240, 242, 244, 246, 248, 251, 253, 255
};

// --------------------------------------------------------------
ColorRamp5LUT::GammaBase ColorRamp5LUT::getGammaBase(float gamma) {
  if (fabsf(gamma - 2.2f) < 0.001f)
    return { dev2lin_22, lin2dev_22, 2.2f };

  static uint8_t dev2lin_tmp[256];
  static uint8_t lin2dev_tmp[256];
  float gi = 1.0f / gamma;
  for (int i=0;i<256;i++)
    dev2lin_tmp[i] = (uint8_t)clamp8((int)(powf(i/255.0f,gi)*255.0f+0.5f));
  for (int i=0;i<256;i++)
    lin2dev_tmp[i] = (uint8_t)clamp8((int)(powf(i/255.0f,gamma)*255.0f+0.5f));
  return { dev2lin_tmp, lin2dev_tmp, gamma };
}

// --------------------------------------------------------------
void ColorRamp5LUT::buildChannelLUT_fast(ChanLUT& L,const GammaBase& base){
  for(int i=0;i<256;i++){
    uint8_t dev=rescale_0_255_to_min_max((uint8_t)i,L.calib.minOut,L.calib.maxOut);
    L.toLinearCal[i]=base.dev2lin[dev];
  }
  for(int i=0;i<256;i++){
    uint8_t d=base.lin2dev[i];
    if(d<L.calib.minOut)d=L.calib.minOut;
    if(d>L.calib.maxOut)d=L.calib.maxOut;
    L.fromLinearFloored[i]=d;
  }
}

// --------------------------------------------------------------
// Constructors (use fast LUT builder)
// --------------------------------------------------------------
ColorRamp5LUT::ColorRamp5LUT(unsigned long fullMs,float gammaAll,Calib calibAll)
: fullDurationMs(fullMs){
  R.gamma=G.gamma=B.gamma=WW.gamma=W.gamma=gammaAll;
  R.calib=G.calib=B.calib=WW.calib=W.calib=calibAll;
  auto base=getGammaBase(gammaAll);
  buildChannelLUT_fast(R,base);buildChannelLUT_fast(G,base);
  buildChannelLUT_fast(B,base);buildChannelLUT_fast(WW,base);
  buildChannelLUT_fast(W,base);
}

ColorRamp5LUT::ColorRamp5LUT(unsigned long fullMs,float gammaAll,
  Calib calibR,Calib calibG,Calib calibB,Calib calibWW,Calib calibW)
: fullDurationMs(fullMs){
  R.gamma=G.gamma=B.gamma=WW.gamma=W.gamma=gammaAll;
  R.calib=calibR;G.calib=calibG;B.calib=calibB;WW.calib=calibWW;W.calib=calibW;
  auto base=getGammaBase(gammaAll);
  buildChannelLUT_fast(R,base);buildChannelLUT_fast(G,base);
  buildChannelLUT_fast(B,base);buildChannelLUT_fast(WW,base);buildChannelLUT_fast(W,base);
}

ColorRamp5LUT::ColorRamp5LUT(unsigned long fullMs,float gR,float gG,float gB,float gWW,float gW,Calib calibAll)
: fullDurationMs(fullMs){
  R.gamma=gR;G.gamma=gG;B.gamma=gB;WW.gamma=gWW;W.gamma=gW;
  R.calib=G.calib=B.calib=WW.calib=W.calib=calibAll;
  buildChannelLUT_fast(R,getGammaBase(gR));
  buildChannelLUT_fast(G,getGammaBase(gG));
  buildChannelLUT_fast(B,getGammaBase(gB));
  buildChannelLUT_fast(WW,getGammaBase(gWW));
  buildChannelLUT_fast(W,getGammaBase(gW));
}

ColorRamp5LUT::ColorRamp5LUT(unsigned long fullMs,float gR,float gG,float gB,float gWW,float gW,
  Calib calibR,Calib calibG,Calib calibB,Calib calibWW,Calib calibW)
: fullDurationMs(fullMs){
  R.gamma=gR;G.gamma=gG;B.gamma=gB;WW.gamma=gWW;W.gamma=gW;
  R.calib=calibR;G.calib=calibG;B.calib=calibB;WW.calib=calibWW;W.calib=calibW;
  buildChannelLUT_fast(R,getGammaBase(gR));buildChannelLUT_fast(G,getGammaBase(gG));
  buildChannelLUT_fast(B,getGammaBase(gB));buildChannelLUT_fast(WW,getGammaBase(gWW));
  buildChannelLUT_fast(W,getGammaBase(gW));
}

// --------------------------------------------------------------
// Setters
// --------------------------------------------------------------
void ColorRamp5LUT::setGammaAll(float g){
  R.gamma=G.gamma=B.gamma=WW.gamma=W.gamma=g;
  auto base=getGammaBase(g);
  buildChannelLUT_fast(R,base);buildChannelLUT_fast(G,base);
  buildChannelLUT_fast(B,base);buildChannelLUT_fast(WW,base);
  buildChannelLUT_fast(W,base);
}

void ColorRamp5LUT::setGammas(float gR,float gG,float gB,float gWW,float gW){
  R.gamma=gR;G.gamma=gG;B.gamma=gB;WW.gamma=gWW;W.gamma=gW;
  buildChannelLUT_fast(R,getGammaBase(gR));buildChannelLUT_fast(G,getGammaBase(gG));
  buildChannelLUT_fast(B,getGammaBase(gB));buildChannelLUT_fast(WW,getGammaBase(gWW));
  buildChannelLUT_fast(W,getGammaBase(gW));
}

void ColorRamp5LUT::setCalibAll(uint8_t minOut,uint8_t maxOut){
  R.calib=G.calib=B.calib=WW.calib=W.calib={minOut,maxOut};
  buildChannelLUT_fast(R,getGammaBase(R.gamma));buildChannelLUT_fast(G,getGammaBase(G.gamma));
  buildChannelLUT_fast(B,getGammaBase(B.gamma));buildChannelLUT_fast(WW,getGammaBase(WW.gamma));
  buildChannelLUT_fast(W,getGammaBase(W.gamma));
}

void ColorRamp5LUT::setCalibPerChannel(Calib r,Calib g,Calib b,Calib ww,Calib w){
  R.calib=r;G.calib=g;B.calib=b;WW.calib=ww;W.calib=w;
  buildChannelLUT_fast(R,getGammaBase(R.gamma));buildChannelLUT_fast(G,getGammaBase(G.gamma));
  buildChannelLUT_fast(B,getGammaBase(B.gamma));buildChannelLUT_fast(WW,getGammaBase(WW.gamma));
  buildChannelLUT_fast(W,getGammaBase(W.gamma));
}

// --------------------------------------------------------------
// Core logic
// --------------------------------------------------------------
float ColorRamp5LUT::maxPerceptualDistance(const Channels&a,const Channels&b)const{
  uint8_t dr=abs((int)R.toLinearCal[b.r]-(int)R.toLinearCal[a.r]);
  uint8_t dg=abs((int)G.toLinearCal[b.g]-(int)G.toLinearCal[a.g]);
  uint8_t db=abs((int)B.toLinearCal[b.b]-(int)B.toLinearCal[a.b]);
  uint8_t dww=abs((int)WW.toLinearCal[b.ww]-(int)WW.toLinearCal[a.ww]);
  uint8_t dw=abs((int)W.toLinearCal[b.w]-(int)W.toLinearCal[a.w]);
  uint8_t dmax=dr;if(dg>dmax)dmax=dg;if(db>dmax)dmax=db;
  if(dww>dmax)dmax=dww;if(dw>dmax)dmax=dw;
  return (float)dmax/255.0f;
}

void ColorRamp5LUT::start(const Channels&cur,const Channels&tgt,unsigned long now){
  startLog=cur;targetLog=tgt;
  float maxNorm=maxPerceptualDistance(startLog,targetLog);
  if(maxNorm<=0.f||fullDurationMs==0){durationMs=0;startTimeMs=now;active=false;return;}
  durationMs=(unsigned long)((double)fullDurationMs*(double)maxNorm);
  if(durationMs==0)durationMs=1;
  startTimeMs=now;active=true;prevValid=false;
}

void ColorRamp5LUT::retargetFromCurrent(const Channels& newTarget, unsigned long now) {
  Channels cur = get(now);
  start(cur, newTarget, now);
}

void ColorRamp5LUT::retargetOnlyW(uint8_t newW, unsigned long now) {
  Channels t = targetLog; t.w = newW;
  retargetFromCurrent(t, now);
}

ColorRamp5LUT::Channels ColorRamp5LUT::computeOut(unsigned long now)const{
  if(!active)
    return {
      R.fromLinearFloored[R.toLinearCal[targetLog.r]],
      G.fromLinearFloored[G.toLinearCal[targetLog.g]],
      B.fromLinearFloored[B.toLinearCal[targetLog.b]],
      WW.fromLinearFloored[WW.toLinearCal[targetLog.ww]],
      W.fromLinearFloored[W.toLinearCal[targetLog.w]]
    };

  unsigned long e=now-startTimeMs;
  if(e>=durationMs)
    return {
      R.fromLinearFloored[R.toLinearCal[targetLog.r]],
      G.fromLinearFloored[G.toLinearCal[targetLog.g]],
      B.fromLinearFloored[B.toLinearCal[targetLog.b]],
      WW.fromLinearFloored[WW.toLinearCal[targetLog.ww]],
      W.fromLinearFloored[W.toLinearCal[targetLog.w]]
    };

  float p=(float)e/(float)durationMs;
  uint8_t lr=lerp8(R.toLinearCal[startLog.r],R.toLinearCal[targetLog.r],p);
  uint8_t lg=lerp8(G.toLinearCal[startLog.g],G.toLinearCal[targetLog.g],p);
  uint8_t lb=lerp8(B.toLinearCal[startLog.b],B.toLinearCal[targetLog.b],p);
  uint8_t lww=lerp8(WW.toLinearCal[startLog.ww],WW.toLinearCal[targetLog.ww],p);
  uint8_t lw=lerp8(W.toLinearCal[startLog.w],W.toLinearCal[targetLog.w],p);

  return {
    R.fromLinearFloored[lr],
    G.fromLinearFloored[lg],
    B.fromLinearFloored[lb],
    WW.fromLinearFloored[lww],
    W.fromLinearFloored[lw]
  };
}

ColorRamp5LUT::Channels ColorRamp5LUT::get(unsigned long now){
  Channels out=computeOut(now);
  if(active && (now-startTimeMs>=durationMs)) active=false;
  return out;
}

ColorRamp5LUT::Channels ColorRamp5LUT::getWithFlags(unsigned long now, Flags& changed){
  Channels out=computeOut(now);

  auto deltaChanged=[&](uint8_t a,uint8_t b)->bool{
    int d=(int)a-(int)b; if(d<0)d=-d;
    return d>(int)changeThreshold;
  };

  if(!prevValid){
    changed={true,true,true,true,true};
    prevValid=true;
  }else{
    changed.r = deltaChanged(out.r ,prevOut.r);
    changed.g = deltaChanged(out.g ,prevOut.g);
    changed.b = deltaChanged(out.b ,prevOut.b);
    changed.ww= deltaChanged(out.ww,prevOut.ww);
    changed.w = deltaChanged(out.w ,prevOut.w);
  }

  prevOut=out;
  if(active && (now-startTimeMs>=durationMs)) active=false;
  return out;
}
