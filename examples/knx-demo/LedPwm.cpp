#include "LedPwm.h"

static HardwareTimer timer3(TIM3); // Use TIM3
static HardwareTimer timer2(TIM2); // Use TIM2

void initPwm(){
    pinMode(PB11, OUTPUT); //R
    pinMode(PB10, OUTPUT); //G
    pinMode(PB1, OUTPUT); //B
    pinMode(PB0, OUTPUT); //WW
    pinMode(PA7, OUTPUT); //W

    timer2.setPWM(4, PB11, pwmFrequency, 0); // R
    timer2.setPWM(3, PB10, pwmFrequency, 0); // G
    timer3.setPWM(4, PB1, pwmFrequency, 0); // B
    timer3.setPWM(3, PB0, pwmFrequency, 0); // WW
    timer3.setPWM(2, PA7, pwmFrequency, 0); // W
}

void setPwmR(uint8_t  dutyCycle)
{
    // Get current auto-reload value (timer top)
    uint32_t arr = timer2.getOverflow();

    // Scale 0..255 to 0..arr
    uint32_t ticks = (uint32_t) ((uint32_t)dutyCycle * arr) / 255U;

    timer2.setCaptureCompare(4, ticks, TICK_COMPARE_FORMAT);
}

void setPwmG(uint8_t  dutyCycle)
{
    // Get current auto-reload value (timer top)
    uint32_t arr = timer2.getOverflow();

    // Scale 0..255 to 0..arr
    uint32_t ticks = (uint32_t) ((uint32_t)dutyCycle * arr) / 255U;
    
    timer2.setCaptureCompare(3, ticks, TICK_COMPARE_FORMAT);
}

void setPwmB(uint8_t  dutyCycle)
{
    // Get current auto-reload value (timer top)
    uint32_t arr = timer3.getOverflow();

    // Scale 0..255 to 0..arr
    uint32_t ticks = (uint32_t) ((uint32_t)dutyCycle * arr) / 255U;
    
    timer3.setCaptureCompare(4, ticks, TICK_COMPARE_FORMAT);
}

void setPwmWW(uint8_t  dutyCycle)
{
    // Get current auto-reload value (timer top)
    uint32_t arr = timer3.getOverflow();

    // Scale 0..255 to 0..arr
    uint32_t ticks = (uint32_t) ((uint32_t)dutyCycle * arr) / 255U;
    
    timer3.setCaptureCompare(3, ticks, TICK_COMPARE_FORMAT);
}

void setPwmW(uint8_t dutyCycle)
{
    // Get current auto-reload value (timer top)
    uint32_t arr = timer3.getOverflow();

    // Scale 0..255 to 0..arr
    uint32_t ticks = (uint32_t) ((uint32_t)dutyCycle * arr) / 255U;
    
    timer3.setCaptureCompare(2, ticks, TICK_COMPARE_FORMAT);
}