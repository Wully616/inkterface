#if defined(LCD5B_MINIMAL_SERIAL_TEST)

#include <Arduino.h>
#include <esp_system.h>

void setup()
{
    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println("MINIMAL SERIAL TEST STARTED");
    Serial.printf("Reset reason: %d\n", static_cast<int>(esp_reset_reason()));
}

void loop()
{
    static uint32_t count = 0;
    Serial.printf("MINIMAL APP RUNNING | heartbeat=%lu | uptime=%lu ms\n",
                  static_cast<unsigned long>(++count),
                  static_cast<unsigned long>(millis()));
    delay(1000);
}

#endif
