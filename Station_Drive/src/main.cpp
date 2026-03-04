#include <Arduino.h>

void setup()
{  
  Serial.begin(115200);
}

void loop()
{
  if (Serial.available())
  {
      char buf[30];
      int readCnt = Serial.readBytes(buf, 30);
      buf[readCnt] = '\0';
      Serial.printf("String = %s\n", buf);
  }
}
