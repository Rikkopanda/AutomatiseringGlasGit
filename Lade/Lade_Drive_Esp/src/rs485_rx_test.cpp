
#include <Arduino.h>

#define RXD2 16
#define TXD2 17

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial2.begin(9600, SERIAL_8N2, RXD2, TXD2);

    Serial.println();
    Serial.println("RS485 RX TEST");
}

void loop()
{
    while (Serial2.available())
    {
        uint8_t b = Serial2.read();

        Serial.printf(
            "RX: 0x%02X  '%c'\n",
            b,
            (b >= 32 && b <= 126) ? b : '.'
        );
    }
}