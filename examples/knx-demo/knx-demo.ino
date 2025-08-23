#include <Arduino.h>
#include <knx.h>

#if MASK_VERSION != 0x07B0 && (defined ARDUINO_ARCH_ESP8266 || defined ARDUINO_ARCH_ESP32)
#include <WiFiManager.h>
#endif

// create named references for easy access to group objects
#define goCurrent knx.getGroupObject(1)
#define goMax knx.getGroupObject(2)
#define goMin knx.getGroupObject(3)
#define goReset knx.getGroupObject(4)

float currentValue = 0;
float maxValue = 0;
float minValue = RAND_MAX;
long lastsend = 0;
HardwareTimer *timer3 = new HardwareTimer(TIM3); // Use TIM3
HardwareTimer *timer2 = new HardwareTimer(TIM2); // Use TIM2

void measureTemp()
{
    long now = millis();
    if ((now - lastsend) < 2000)
        return;

    lastsend = now;
    int r = rand();
    currentValue = (r * 1.0) / (RAND_MAX * 1.0);
    currentValue *= 100 * 100;

    // write new value to groupobject
    goCurrent.value(currentValue);

    if (currentValue > maxValue)
    {
        maxValue = currentValue;
        goMax.value(maxValue);
    }

    if (currentValue < minValue)
    {
        minValue = currentValue;
        goMin.value(minValue);
    }
}

// callback from reset-GO
void resetCallback(GroupObject& go)
{
    if (go.value())
    {
        maxValue = 0;
        minValue = 10000;
    }
}

void setupPwmChannels(){
    pinMode(PB11, OUTPUT); //R
    pinMode(PB10, OUTPUT); //G
    pinMode(PB1, OUTPUT); //B
    pinMode(PB0, OUTPUT); //WW
    pinMode(PA7, OUTPUT); //W

    timer2->setPWM(4, PB11, 15000, 50); // R
    timer2->setPWM(3, PB10, 15000, 50); // G

    timer3->setPWM(4, PB1, 15000, 50); // B
    timer3->setPWM(3, PB0, 15000, 50); // WW
    timer3->setPWM(2, PA7, 15000, 50); // W
}

void setup()
{
    setupPwmChannels();

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
        // register callback for reset GO
        goReset.callback(resetCallback);
        goReset.dataPointType(DPT_Trigger);
        goCurrent.dataPointType(DPT_Value_Temp);
        goMin.dataPointType(DPT_Value_Temp);
        goMax.dataPointType(DPT_Value_Temp);

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

    measureTemp();
}
