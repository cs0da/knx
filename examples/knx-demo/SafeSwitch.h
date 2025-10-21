#pragma once
#include <Arduino.h>

#ifdef __GNUC__
#  define FAST_INLINE inline __attribute__((always_inline))
#else
#  define FAST_INLINE inline
#endif

/**
 * SafeSwitch
 * ----------
 * High-speed ON/OFF switch with latched emergency stop.
 * - Direct STM32 GPIO BSRR writes (atomic, constant-time).
 * - Critical methods inline for ISR-speed.
 * - activeHigh = true  -> pin HIGH means ON
 *   activeHigh = false -> pin LOW  means ON.
 */
class SafeSwitch {
public:
    explicit SafeSwitch(uint8_t pin, bool activeHigh = true) noexcept;

    // Configure pin as output and force OFF. Call once in setup().
    void begin() noexcept;

    // ---- Critical hot paths (inline) ----
    FAST_INLINE bool on() noexcept {
        if (_emergency) return false;
        if (_isOn)      return true;
        writeOn_();
        _isOn = true;
        return true;
    }

    FAST_INLINE bool off() noexcept {
        if (!_isOn) return false;
        writeOff_();
        _isOn = false;
        return true;
    }

    FAST_INLINE void emergencyTrip() noexcept {
        if (_isOn) {
            writeOff_();    // immediate hardware cut
            _isOn = false;
        }
        _emergency = true;   // latch
    }

    FAST_INLINE void emergencyReset() noexcept { _emergency = false; }

    // ISR helpers (skip “already on” check)
    FAST_INLINE void onISR() noexcept {
        if (!_emergency) { writeOn_(); _isOn = true; }
    }

    FAST_INLINE void offISR() noexcept {
        writeOff_();
        _isOn = false;
    }

    // State queries
    FAST_INLINE bool isOn() const noexcept            { return _isOn; }
    FAST_INLINE bool isEmergencyLatched() const noexcept { return _emergency; }

private:
    // Internal low-level writes
    FAST_INLINE void writeOn_() noexcept {
        if (_activeHigh) _port->BSRR = _setBSRR;
        else             _port->BSRR = _rstBSRR;
    }
    FAST_INLINE void writeOff_() noexcept {
        if (_activeHigh) _port->BSRR = _rstBSRR;
        else             _port->BSRR = _setBSRR;
    }
    FAST_INLINE void forceOff_() noexcept {
        if (_activeHigh) _port->BSRR = _rstBSRR;
        else             _port->BSRR = _setBSRR;
        _isOn = false;
    }

    GPIO_TypeDef* _port{nullptr};
    uint32_t      _bit{0};
    uint32_t      _setBSRR{0};
    uint32_t      _rstBSRR{0};
    uint8_t       _pin{0};
    bool          _activeHigh{true};

    volatile bool _isOn{false};
    volatile bool _emergency{false};
};
