#include <Arduino.h>
#include <knx.h>
#include <ColorRamp5LUT.h>
#include "CCTMixer.h"
#include "Helper.h"
#include "SafeSwitch.h"

#if MASK_VERSION != 0x07B0 && (defined ARDUINO_ARCH_ESP8266 || defined ARDUINO_ARCH_ESP32)
#include <WiFiManager.h>
#endif

// create named references for easy access to group objects
#define goColorRGBW knx.getGroupObject(1) // DPT Colour_RGBW -> Control W as one -> do not modify the set color temp value.
#define goColorRGB knx.getGroupObject(2) // DPT_Colour_RGB
#define goColorTemperature knx.getGroupObject(3) // DPT_Absolute_Colour_Temperature
#define goSwitch knx.getGroupObject(4) // DPT_Switch
/*
#define goColorCCT knx.getGroupObject(4) // DPT_Brightness_Colour_Temperature_Transition ???
#define goCurrent knx.getGroupObject(5) // DPT_Value_Curr
#define goSwitch knx.getGroupObject(6) // DPT_Switch
#define goLightError knx.getGroupObject(7) // DPT_LightActuatorErrorInfo
*/
// #define goColorWarm knx.getGroupObject(8) // DPT_Percent_U8
// #define goColorCold knx.getGroupObject(9) // DPT_Percent_U8

const unsigned int PwmFrequency = 15000;
const uint8_t PwmRefreshInMs = 10;

#define pwmValueArraySize 6
#define valueIndexR 0
#define valueIndexG 1
#define valueIndexB 2
#define valueIndexW 3
#define valueIndexColorTemp 4
#define valueIndexSwitch 5

struct KnxReceivedValue
{
    uint16_t value;
};

volatile KnxReceivedValue knxValues[pwmValueArraySize];

volatile bool newColorPending = false;

volatile bool newSwitchValuePending = false;

bool powerOffPending = false;


HardwareTimer timer3(TIM3); // Use TIM3
HardwareTimer timer2(TIM2); // Use TIM2

//TODO: create the object after the KNX initialization. Also, try to save the lookup tables to eeprom or SPI NOR and load it from there
ColorRamp5LUT colorRamp5(1200, 2.2f);
CCTMixer colorTempMixer(2700, 4000, 6500);
SafeSwitch powerSwitch(PB5, true);

void initPwm(){
    pinMode(PB11, OUTPUT); //R
    pinMode(PB10, OUTPUT); //G
    pinMode(PB1, OUTPUT); //B
    pinMode(PB0, OUTPUT); //WW
    pinMode(PA7, OUTPUT); //W

    timer2.setPWM(4, PB11, PwmFrequency, 50); // R
    timer2.setPWM(3, PB10, PwmFrequency, 50); // G
    timer3.setPWM(4, PB1, PwmFrequency, 50); // B
    timer3.setPWM(3, PB0, PwmFrequency, 50); // WW
    timer3.setPWM(2, PA7, PwmFrequency, 50); // W
}

void setPwmR(u_int8_t  dutyCycle)
{
    // Get current auto-reload value (timer top)
    uint32_t arr = timer2.getOverflow();

    // Scale 0..255 to 0..arr
    uint32_t ticks = (uint32_t) ((uint32_t)dutyCycle * arr) / 255U;

    timer2.setCaptureCompare(4, ticks, TICK_COMPARE_FORMAT);
}

void setPwmG(u_int8_t  dutyCycle)
{
    // Get current auto-reload value (timer top)
    uint32_t arr = timer2.getOverflow();

    // Scale 0..255 to 0..arr
    uint32_t ticks = (uint32_t) ((uint32_t)dutyCycle * arr) / 255U;
    
    timer2.setCaptureCompare(3, ticks, TICK_COMPARE_FORMAT);
}

void setPwmB(u_int8_t  dutyCycle)
{
    // Get current auto-reload value (timer top)
    uint32_t arr = timer3.getOverflow();

    // Scale 0..255 to 0..arr
    uint32_t ticks = (uint32_t) ((uint32_t)dutyCycle * arr) / 255U;
    
    timer3.setCaptureCompare(4, ticks, TICK_COMPARE_FORMAT);
}

void setPwmWW(u_int8_t  dutyCycle)
{
    // Get current auto-reload value (timer top)
    uint32_t arr = timer3.getOverflow();

    // Scale 0..255 to 0..arr
    uint32_t ticks = (uint32_t) ((uint32_t)dutyCycle * arr) / 255U;
    
    timer3.setCaptureCompare(3, ticks, TICK_COMPARE_FORMAT);
}

void setPwmW(u_int8_t dutyCycle)
{
    // Get current auto-reload value (timer top)
    uint32_t arr = timer3.getOverflow();

    // Scale 0..255 to 0..arr
    uint32_t ticks = (uint32_t) ((uint32_t)dutyCycle * arr) / 255U;
    
    timer3.setCaptureCompare(2, ticks, TICK_COMPARE_FORMAT);
}

bool switchStatus()
{
    const uint8_t* data = goSwitch.valueRef();
    size_t len = goSwitch.sizeInTelegram();

    if (len >= 1) 
    {
        return data[0];
    }   

    return false;
}

void setSwitch(GroupObject& go)
{
    const uint8_t* data = go.valueRef();
    // size_t len = go.sizeInTelegram();

    uint8_t switchValue = data[0];
    uint8_t previousValue = knxValues[valueIndexSwitch].value;

    if (switchValue != previousValue)
    {
        knxValues[valueIndexSwitch].value = switchValue;

        newSwitchValuePending = true;
        Serial.printf("Switch status wrote.\n");
    }

    Serial.printf("Switch = %u\n", switchValue);
}

void setColorRgbw(GroupObject& go)
{
    const uint8_t* data = go.valueRef();
    size_t len = go.sizeInTelegram();

    if (len >= 4) {
        uint8_t r = data[0];
        uint8_t g = data[1];
        uint8_t b = data[2];
        uint8_t w = data[3];

        // TODO: received in 0..255 not in percent!
        knxValues[valueIndexR].value = r;
        knxValues[valueIndexG].value = g;
        knxValues[valueIndexB].value = b;
        knxValues[valueIndexW].value = w;

        newColorPending = true;

        Serial.printf("RGBW = %u, %u, %u, %u\n", r, g, b, w);
    } else {
        Serial.printf("unexpected payload length: %u\n", (unsigned)len);
    }
}

void setColorRgb(GroupObject& go)
{
    const uint8_t* data = go.valueRef();
    size_t len = go.sizeInTelegram();
    
    if (len >= 3) {
        // Received data is in 0..255 range
        uint8_t r = data[0];
        uint8_t g = data[1];
        uint8_t b = data[2];

        knxValues[valueIndexR].value = data[0];
        knxValues[valueIndexG].value = data[1];
        knxValues[valueIndexB].value = data[2];

        newColorPending = true;

        Serial.printf("RGB = %u, %u, %u\n", r, g, b);
    } 
    else 
    {
        Serial.printf("unexpected payload length: %u\n", (unsigned)len);
    }
}

void setColorTemperature(GroupObject& go)
{
    const uint8_t* data = go.valueRef();
    size_t len = go.sizeInTelegram();

    if (len >= 2) 
    {
        uint16_t colorTemperature = (data[0] << 8) | data[1]; //TODO: Check if logic is okay.
        knxValues[valueIndexColorTemp].value = colorTemperature;

        newColorPending = true;
        
        Serial.printf("Color temp = %u\n", colorTemperature);
    }
    else
    {
        Serial.printf("unexpected payload length: %u\n", (unsigned)len);
    }
}

void setup()
{
    initPwm();

    powerSwitch.begin();

    Serial.begin(115200);
    ArduinoPlatform::SerialDebug = &Serial;

    randomSeed(millis());

#if MASK_VERSION != 0x07B0 && (defined ARDUINO_ARCH_ESP8266 || defined ARDUINO_ARCH_ESP32)
    WiFiManager wifiManager;
    wifiManager.autoConnect("knx-demo");
#endif

    // read adress table, association table, groupobject table and parameters from eeprom
    knx.readMemory();

    // print values of parameters if device is already configured
    if (knx.configured())
    {
        Serial.println("Registering callback");
        goColorRGBW.callback(setColorRgbw);
        Serial.println("Setting data point type");
        // register callback for reset GO
        goColorRGBW.dataPointType(DPT_Colour_RGBW);
        Serial.println("Done");

        
        goColorRGB.dataPointType(DPT_Colour_RGB);
        goColorRGB.callback(setColorRgb);

        goColorTemperature.dataPointType(DPT_Absolute_Colour_Temperature);
        goColorTemperature.callback(setColorTemperature);

        goSwitch.dataPointType(DPT_Switch);
        goSwitch.callback(setSwitch);

        /*
        // goColorWarm.dataPointType(DPT_Percent_U8); // Register callback to set the color - maybe not required, can be set if color temp
        // goColorCold.dataPointType(DPT_Percent_U8); // Register callback to set the color - maybe not required, can be set if color temp
        goCurrent.dataPointType(DPT_Value_Curr); // Read only - do not send this value out automatically.

        goLightError.dataPointType(DPT_LightActuatorErrorInfo); // Readonly -> auto send
        */

        Serial.print("Timeout: ");
        Serial.println(knx.paramByte(0));
        Serial.print("Zykl. senden: ");
        Serial.println(knx.paramByte(1));
        Serial.print("Min/Max senden: ");
        Serial.println(knx.paramByte(2));
        Serial.print("Aenderung senden: ");
        Serial.println(knx.paramByte(3));
        Serial.print("Abgleich: ");
        Serial.println(knx.paramByte(4));
    }

    // pin or GPIO the programming led is connected to. Default is LED_BUILTIN
    // knx.ledPin(LED_BUILTIN);
    // is the led active on HIGH or low? Default is LOW
    // knx.ledPinActiveOn(HIGH);
    // pin or GPIO programming button is connected to. Default is 0
    // knx.buttonPin(0);

    // start the framework.
    knx.start();
}

void loop()
{
    // don't delay here to much. Otherwise you might lose packages or mess up the timing with ETS
    knx.loop();

    // only run the application code if the device was configured with ETS
    if (!knx.configured())
        return;

    handlePowerSwitch();

    handleColorChange();
}

void handlePowerSwitch()
{
    if (powerOffPending && !colorRamp5.isActive())
    {
        powerOffPending = false;

        powerSwitch.off();
        Serial.printf("Power Off\n");
    }

    if (newSwitchValuePending)
    {
        bool switchValue;

        noInterrupts();
        switchValue = knxValues[valueIndexSwitch].value;
        newSwitchValuePending = false;
        interrupts();

        if (switchValue == false)
        {
            powerOffPending = true;
        }
        else
        {
            newColorPending = true;

            powerOffPending = false;

            powerSwitch.on();

            Serial.printf("Power On\n");
        }
    }
}

void handleColorChange()
{
    static u_int32_t lastPwmRefreshTime;
    static bool emergancyHandled;
    static bool powerOffHandled;

    auto currentTime = millis();

    if (!powerSwitch.isEmergencyLatched())
    {
        emergancyHandled = false;

        if (newColorPending && !powerOffPending && powerSwitch.isOn()) 
            {
                powerOffHandled = false;

                uint8_t r;
                uint8_t g;
                uint8_t b;
                uint8_t w;
                u_int16_t colorTemp;

                noInterrupts();               // small critical section if interrupt driven
                
                r = knxValues[valueIndexR].value;
                g = knxValues[valueIndexG].value;
                b = knxValues[valueIndexB].value;
                w = knxValues[valueIndexW].value;
                colorTemp = knxValues[valueIndexColorTemp].value;
                newColorPending = false;

                interrupts();

                // TODO: modify the compute to accept 0-255 value insted of percentage.
                // Calculate CW and WW colors based on color temperature.
                auto cwww = colorTempMixer.compute(colorTemp, Helper::byteToPct(w));

                ColorRamp5LUT::Channels next;
                next.r = r;
                next.g = g;
                next.b = b;
                next.ww = cwww.warmOut;
                next.w = cwww.coldOut;

                // Smoothly retarget from the *current* levels to the new colour
                colorRamp5.retargetFromCurrent(next, currentTime);
            }
            else if(powerOffPending && !powerOffHandled) 
            {
                powerOffHandled = true;

                ColorRamp5LUT::Channels next;
                next.r = 0;
                next.g = 0;
                next.b = 0;
                next.ww = 0;
                next.w = 0;

                colorRamp5.retargetFromCurrent(next, currentTime);
            }

            if (colorRamp5.isActive())
            {
                if ((lastPwmRefreshTime + PwmRefreshInMs) < currentTime)
                {
                    ColorRamp5LUT::Flags colorChanged;

                    lastPwmRefreshTime = currentTime;

                    auto currentColors = colorRamp5.getWithFlags(currentTime, colorChanged);
                    
                    // Valid value: 0..255
                    if (colorChanged.r) setPwmR(currentColors.r);
                    if (colorChanged.g) setPwmG(currentColors.g);
                    if (colorChanged.b) setPwmB(currentColors.b);
                    if (colorChanged.ww) setPwmWW(currentColors.ww);
                    if (colorChanged.w) setPwmW(currentColors.w);
                }
            }
    }
    else if (!emergancyHandled)
    {
        setPwmR(0);
        setPwmG(0);
        setPwmB(0);
        setPwmWW(0);
        setPwmW(0);

        emergancyHandled = true;
    }
    
    // if (switchStatus() != powerSwitch.isOn())
    // {
    //     goSwitch.value() = powerSwitch.isOn();
    // }
}