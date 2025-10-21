#include "SafeSwitch.h"

SafeSwitch::SafeSwitch(uint8_t pin, bool activeHigh) noexcept
: _pin(pin), _activeHigh(activeHigh)
{
    _port    = (GPIO_TypeDef*)digitalPinToPort(pin);
    _bit     = digitalPinToBitMask(pin);
    _setBSRR = _bit;            // lower 16 bits set the pin
    _rstBSRR = (_bit << 16U);   // upper 16 bits reset the pin
}

void SafeSwitch::begin() noexcept {
    pinMode(_pin, OUTPUT);
    forceOff_();                // ensure OFF at startup
}
