#include <Arduino.h>

#define RXD2 16
#define TXD2 17

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial2.begin(9600, SERIAL_8N2, RXD2, TXD2);

    Serial.println();
    Serial.println("RS485 TX TEST");
}

void loop()
{
    Serial.println("Sending AAAAA.....");

    for (int i = 0; i < 20; i++)
    {
        Serial2.write(0x55);
    }

    Serial2.flush();

    delay(1000);
}
