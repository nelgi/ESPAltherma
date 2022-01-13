#include <Arduino.h>
#ifdef ESP32
#include <HardwareSerial.h>

HardwareSerial MySerial(1);
#endif
#define SER_TIMEOUT 300 //leave 300ms for the machine to answer

char getCRC(char *src, int len)
{
  char b = 0;
  for (int i = 0; i < len; i++)
  {
    b += src[i];
  }
  return ~b;
}

bool queryRegistry(char regID, char *buffer)
{

  //preparing command:
  char prep[] = {0x03, 0x40, regID, 0x00};
  prep[3] = getCRC(prep, 3);

  //Sending command to serial
  #ifdef ESP32
  MySerial.flush(); //Prevent possible pending info on the read
  MySerial.write(prep, 4);
  #elif ESP8266
  Serial.flush(); //Prevent possible pending info on the read
  Serial.write(prep, 4);
  #endif
  ulong start = millis();

  int len = 0;
  buffer[2] = 10;
  mqttSerial.printf("Querying register 0x%02x... ", regID);
  while ((len < buffer[2] + 2) && (millis() < (start + SER_TIMEOUT)))
  {
    #ifdef ESP32
    if (MySerial.available())
    {
      buffer[len++] = MySerial.read();
    }
    #elif ESP8266
    if (Serial.available())
    {
      buffer[len++] = Serial.read();
    }
    #endif
  }
  if (millis() >= (start + SER_TIMEOUT))
  {
    if (len == 0)
    {
      mqttSerial.printf("Time out! Check connection\n");
    }
    else
    {
      mqttSerial.printf("ERR: Time out on register 0x%02x! got %d/%d bytes\n", regID, len, buffer[2]);
      char bufflog[250] = {0};
      for (size_t i = 0; i < len; i++)
      {
        sprintf(bufflog + i * 5, "0x%02x ", buffer[i]);
      }
      mqttSerial.print(bufflog);
    }
    delay(500);
    return false;
  }
  if (getCRC(buffer, len - 1) != buffer[len - 1])
  {
    #ifdef ESP32
    Serial.println("Wrong CRC!");
    Serial.printf("Calculated 0x%2x but got 0x%2x\n", getCRC(buffer, len - 1), buffer[len - 1]);
    #endif
    mqttSerial.printf("ERROR: Wrong CRC on register 0x%02x!", regID);
    return false;
  }
  else
  {
    #ifdef ESP32
    Serial.println(".. CRC OK!");
    #endif
    return true;
  }
}