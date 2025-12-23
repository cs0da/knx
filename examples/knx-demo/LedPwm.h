#pragma once
#include <Arduino.h>

#define pwmFrequency 15000

void initPwm();

void setPwmR(uint8_t  dutyCycle);

void setPwmG(uint8_t  dutyCycle);

void setPwmB(uint8_t  dutyCycle);

void setPwmW(uint8_t  dutyCycle);

void setPwmWW(uint8_t  dutyCycle);