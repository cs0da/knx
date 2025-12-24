#include <Arduino.h>
#include <knx.h>
#include <ColorRamp5LUT.h>
#include "CCTMixer.h"
#include "Helper.h"
#include "SafeSwitch.h"
#include "adc_driver.h"
#include "LedPwm.h"

#if MASK_VERSION != 0x07B0 && (defined ARDUINO_ARCH_ESP8266 || defined ARDUINO_ARCH_ESP32)
#include <WiFiManager.h>
#endif

// create named references for easy access to group objects
#define goColorRGBW knx.getGroupObject(1) // DPT Colour_RGBW -> Control W as one -> do not modify the set color temp value.
#define goColorRGB knx.getGroupObject(2) // DPT_Colour_RGB
#define goColorTemperature knx.getGroupObject(3) // DPT_Absolute_Colour_Temperature
#define goSwitch knx.getGroupObject(4) // DPT_Switch
#define goBrightness knx.getGroupObject(5) // DPT_Switch
#define goWhite knx.getGroupObject(6) // DPT_Switch
#define goDimming knx.getGroupObject(7) // DPT_Switch
/*
#define goColorCCT knx.getGroupObject(4) // DPT_Brightness_Colour_Temperature_Transition ???
#define goCurrent knx.getGroupObject(5) // DPT_Value_Curr
#define goSwitch knx.getGroupObject(6) // DPT_Switch
#define goLightError knx.getGroupObject(7) // DPT_LightActuatorErrorInfo
*/
// #define goColorWarm knx.getGroupObject(8) // DPT_Percent_U8
// #define goColorCold knx.getGroupObject(9) // DPT_Percent_U8

#define pwmRefreshInMs 10
#define errorSignalBlinkTimeMs 500
#define errorResetTimeInMs 5000
#define startupLedIndicationMs 5000

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
bool colorErrorResetHandled = true;

//TODO: create the object after the KNX initialization. Also, try to save the lookup tables to eeprom or SPI NOR and load it from there
ColorRamp5LUT colorRamp5(1200, 2.2f, {0, 200}, {0, 200}, {0, 200}, {0, 255}, {0, 255});
CCTMixer colorTempMixer(2700, 4000, 6500);
SafeSwitch powerSwitch(PB5, true);

void writeToSwitch(bool state)
{
    uint8_t* dataPtr = goSwitch.valueRef(); 

    dataPtr[0] = state ? 1 : 0;
}

void initAdcAndStart()
{
    adcInitDualWithVref_NonBlocking(
      50,
      32,
      ADC_SAMPLETIME_160CYCLES_5,   // long S/H; speed up later if needed
      2,
      0.005f,
      50
    );
    // Start the first acquisition
    adcRequestBatch();
}

void initUserInterface()
{
    // Notification LED
    pinMode(PA9, OUTPUT);
    digitalWrite(PA9, HIGH);

    // Button
    pinMode(PB6, INPUT);
}

inline void userLedOn()
{
    digitalWrite(PA9, HIGH);
}

inline void userLedOff()
{
    digitalWrite(PA9, LOW);
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
    if (powerSwitch.isEmergencyLatched())
    {
        writeToSwitch(false);
        return;
    }

    const uint8_t* data = go.valueRef();

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

extern "C" void SystemClock_Config(void) {
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  // Configure the main internal regulator output voltage
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  // Configure external clock on PH0
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_BYPASS;  // For external clock input
  // Use RCC_HSE_ON if using crystal oscillator
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLLMUL_4;      // Adjust multiplier
  RCC_OscInitStruct.PLL.PLLDIV = RCC_PLLDIV_2;      // Adjust divider
  
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
    Error_Handler();
  }

  // Configure the system clock
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_1) != HAL_OK) {
    Error_Handler();
  }
}

void setup()
{
    initUserInterface();
    initAdcAndStart();
    initPwm();

    powerSwitch.begin();

    knxValues[valueIndexR].value = 127;
    knxValues[valueIndexG].value = 127;
    knxValues[valueIndexB].value = 127;
    knxValues[valueIndexW].value = 127;
    knxValues[valueIndexColorTemp].value = 4100;

    Serial.begin(115200);
    ArduinoPlatform::SerialDebug = &Serial;

// #ifdef LIBRETINY
//     srandom(millis());
// #else
//     randomSeed(millis());
// #endif


    // read adress table, association table, groupobject table and parameters from eeprom
    knx.readMemory();

    // print values of parameters if device is already configured
    if (knx.configured())
    {
        goColorRGBW.dataPointType(DPT_Colour_RGBW);
        goColorRGBW.callback(setColorRgbw);
        
        goColorRGB.dataPointType(DPT_Colour_RGB);
        goColorRGB.callback(setColorRgb);

        goColorTemperature.dataPointType(DPT_Absolute_Colour_Temperature);
        goColorTemperature.callback(setColorTemperature);

        goSwitch.dataPointType(DPT_Switch);
        goSwitch.callback(setSwitch);

        //TODO: Implement callback!
        goBrightness.dataPointType(DPT_Scaling); //0..100%
        goWhite.dataPointType(DPT_DecimalFactor); //0..255
        goDimming.dataPointType(DPT_Control_Dimming);

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

  // 4) (Optional) Prove HSI isn’t needed: turn it off after the switch
  //    If your code keeps running and printing, you’re not using HSI.
  __HAL_RCC_HSI_DISABLE();
}

void loop()
{
    // don't delay here to much. Otherwise you might lose packages or mess up the timing with ETS
    knx.loop();

    // only run the application code if the device was configured with ETS
    if (!knx.configured())
        return;

    auto currentTime = millis();

    handlePowerSwitch();

    handleColorChange(currentTime);

    handleAdc();

    handleUserInterface(currentTime);
}

void handleUserInterface(uint32_t currentTimeMs)
{
    static uint32_t lastPwmRefreshTimeMs = currentTimeMs;
    static bool startupLedIndicationDone = false;

    if (powerSwitch.isEmergencyLatched())
    {
        bool buttonState = digitalRead(PB6);

        if (!buttonState)
        {
            if (currentTimeMs > lastPwmRefreshTimeMs + errorResetTimeInMs)
            {
                powerSwitch.emergencyReset();
                userLedOff();
                colorErrorResetHandled = false;
            }
        }
        else
        {
            lastPwmRefreshTimeMs = currentTimeMs;
        }
    }
    
    if (!startupLedIndicationDone)
    {
        if (currentTimeMs > lastPwmRefreshTimeMs + startupLedIndicationMs)
        {
            userLedOff();
            startupLedIndicationDone = true;
        }
    }
}

void triggerEmergencyShutdown()
{
    powerSwitch.emergencyTrip();
    writeToSwitch(false);
    knxValues[valueIndexSwitch].value = 0;
}

void checkSafetyMargins(AdcBatchResult result)
{
    // Over current
    if (result.i_shunt > 8)
    {
        triggerEmergencyShutdown();
        // TODO: Alert user.
        // Implement handling of reset by the user button
    }
    
    //over voltage
    if (result.v_pa5 > 1.5)
    {
        triggerEmergencyShutdown();
        // TODO: Alert user.
    }
}

void handleAdc()
{
    adcPollProcess();

    if (adcResultAvailable()) 
    {
        AdcBatchResult r = adcGetLastResult(); // clears the ready flag

        // Serial.print("VDDA[V]: ");     Serial.print(r.vdda, 4);
        // Serial.print("  Vmain[V]: ");  Serial.print(r.v_main, 3);
        // Serial.print("  Vpa5[V]: ");   Serial.print(r.v_pa5, 4);
        // Serial.print("  Vsh_mon[V]: ");Serial.print(r.v_shunt_mon, 4);
        // Serial.print("  Ishunt[A]: "); Serial.println(r.i_shunt, 6);

        checkSafetyMargins(r);

        // Start the next batch (non-blocking)
        adcRequestBatch();
  }
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

void errorSignal(uint32_t millis)
{
    static uint32_t lastRefreshTimeMs;
    static bool ledState;

    if (millis > lastRefreshTimeMs + errorSignalBlinkTimeMs)
    {
        lastRefreshTimeMs = millis;

        if (ledState)
        {
            userLedOn();
            ledState = false;
        }
        else
        {
            userLedOff();
            ledState = true;
        }
    }
}

void handleColorChange(uint32_t currentTimeMs)
{
    static uint32_t lastPwmRefreshTimeMs;
    static bool emergancyHandled;
    static bool powerOffHandled;

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

                if(!colorErrorResetHandled)
                {
                    colorErrorResetHandled = true;
                    
                    ColorRamp5LUT::Channels channels;
                    channels.r = 0;
                    channels.g = 0;
                    channels.b = 0;
                    channels.ww = 0;
                    channels.w = 0;

                    colorRamp5.start(channels, next, currentTimeMs);
                }
                else
                {
                    // Smoothly retarget from the *current* levels to the new colour
                    colorRamp5.retargetFromCurrent(next, currentTimeMs);
                }
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

                colorRamp5.retargetFromCurrent(next, currentTimeMs);
            }

            if (colorRamp5.isActive())
            {
                if ((lastPwmRefreshTimeMs + pwmRefreshInMs) < currentTimeMs)
                {
                    ColorRamp5LUT::Flags colorChanged;

                    lastPwmRefreshTimeMs = currentTimeMs;

                    auto currentColors = colorRamp5.getWithFlags(currentTimeMs, colorChanged);
                    
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
    else
    {
        errorSignal(currentTimeMs);
    }
    
    // if (switchStatus() != powerSwitch.isOn())
    // {
    //     goSwitch.value() = powerSwitch.isOn();
    // }
}