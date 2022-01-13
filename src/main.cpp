#ifdef ARDUINO_M5Stick_C_Plus
#include <M5StickCPlus.h>
#elif ARDUINO_M5Stick_C
#include <M5StickC.h>
#else
#include <Arduino.h>
#endif

#ifdef ESP32
#include <HardwareSerial.h>
#include <WiFi.h>
#elif ESP8266
#include <ESP8266WiFi.h>
#endif

#include <PubSubClient.h>
#include <ArduinoOTA.h>

#include "setup.h" //<-- Configure your setup here
#include "mqttserial.h"
#include "converters.h"
#include "comm.h"
#include "mqtt.h"
#define MQTT_LOG_TOPIC MQTT_BASE_TOPIC "/log"

#ifdef ONEWIRE_BUS
#include <OneWire.h>
#include <DallasTemperature.h>
OneWire oneWire(ONEWIRE_BUS);        // Set up a OneWire instance to communicate with OneWire devices
DallasTemperature OnewireSensors(&oneWire); // Create an instance of the temperature sensor class
#endif

Converter converter;
char registryIDs[32]; //Holds the registrys to query
bool busy = false;

#if defined(ARDUINO_M5Stick_C) || defined(ARDUINO_M5Stick_C_Plus)
long LCDTimeout = 40000;//Keep screen ON for 40s then turn off. ButtonA will turn it On again.
#endif

bool contains(char array[], int size, int value)
{
  for (int i = 0; i < size; i++)
  {
    if (array[i] == value)
      return true;
  }
  return false;
}

//Converts to string and add the value to the JSON message
void updateValues(char regID)
{
  LabelDef *labels[128];
  int num = 0;
  converter.getLabels(regID, labels, num);
  for (size_t i = 0; i < num; i++)
  {
    bool alpha = false;
    for (size_t j = 0; j < strlen(labels[i]->asString); j++)
    {
      char c = labels[i]->asString[j];
      if (!isdigit(c) && c!='.'){
        alpha = true;
      }
    }

    #ifdef ONEVAL_ONETOPIC
    char topicBuff[128] = MQTT_OneTopic;
    strcat(topicBuff,labels[i]->label);
    client.publish(topicBuff, labels[i]->asString);
    #endif
    
    if (alpha){      
      snprintf(jsonbuff + strlen(jsonbuff), MAX_MSG_SIZE - strlen(jsonbuff), "\"%s\":\"%s\",", labels[i]->label, labels[i]->asString);
    }
    else{//number, no quotes
      snprintf(jsonbuff + strlen(jsonbuff), MAX_MSG_SIZE - strlen(jsonbuff), "\"%s\":%s,", labels[i]->label, labels[i]->asString);

    }
  }
}

uint16_t loopcount =0;

void extraLoop()
{
  client.loop();
  ArduinoOTA.handle();
  while (busy)
  { //Stop processing during OTA
    ArduinoOTA.handle();
  }

#if defined(ARDUINO_M5Stick_C) || defined(ARDUINO_M5Stick_C_Plus)
  if (M5.BtnA.wasPressed()){//Turn back ON screen
    M5.Axp.ScreenBreath(12);
    LCDTimeout = millis() + 30000;
  }else if (LCDTimeout < millis()){//Turn screen off.
    M5.Axp.ScreenBreath(0);
  }
  M5.update();
#endif
}

void setup_wifi()
{
  delay(10);
  // We start by connecting to a WiFi network
  mqttSerial.printf("Connecting to %s\n", WIFI_SSID);
  WiFi.hostname(HOSTNAME);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PWD);
  int i = 0;
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    #ifdef ESP32
    Serial.print(".");
    #endif
    if (i++ == 100)
    {
      #ifdef ESTP32
      esp_restart();
      #elif ESP8266
      ESP.restart();
      #endif
    }
  }
  mqttSerial.printf("Connected. IP Address: %s\n", WiFi.localIP().toString().c_str());
}

void initRegistries(){
    //getting the list of registries to query from the selected values  
  for (size_t i = 0; i < sizeof(registryIDs); i++)
  {
    registryIDs[i]=0xff;
  }
  
  int i = 0;
  for (auto &&label : labelDefs)
  {
    if (!contains(registryIDs, sizeof(registryIDs), label.registryID))
    {
      mqttSerial.printf("Adding registry 0x%2x to be queried.\n", label.registryID);
      registryIDs[i++] = label.registryID;
    }
  }
  if (i == 0)
  {
    mqttSerial.printf("ERROR - No values selected in the include file. Stopping.\n");
    while (true)
    {
      extraLoop();
    }
  }
}

void setupScreen(){
#if defined(ARDUINO_M5Stick_C) || defined(ARDUINO_M5Stick_C_Plus)
  M5.begin();
  M5.Axp.EnableCoulombcounter();
  M5.Lcd.setRotation(1);
  M5.Axp.ScreenBreath(12);
  M5.Lcd.fillScreen(TFT_WHITE);
  M5.Lcd.setFreeFont(&FreeSansBold12pt7b);
  m5.Lcd.setTextDatum(MC_DATUM);
  int xpos = M5.Lcd.width() / 2; // Half the screen width
  int ypos = M5.Lcd.height() / 2; // Half the screen width
  M5.Lcd.setTextColor(TFT_DARKGREY);
  M5.Lcd.drawString("ESPAltherma", xpos,ypos,1);
  delay(2000);
  M5.Lcd.fillScreen(TFT_BLACK);
  M5.Lcd.setTextFont(1);
  M5.Lcd.setTextColor(TFT_GREEN);
#endif
}

String mac_address(){
  String macAddress;
  macAddress.reserve(6);
  byte mac[6]; 
  WiFi.macAddress(mac);
  for (int i = 2; i >= 0; i--) {
    macAddress += String(mac[i], HEX);
  }
  macAddress.toUpperCase();
  return macAddress;
}

#ifdef ONEWIRE_BUS
void requestTemperatures(){
  mqttSerial.print("Requesting temperatures...");
  OnewireSensors.requestTemperatures(); // Send the command to get temperatures
  // After we got the temperatures, we can print them here.
  // We use the function ByIndex, and as an example get the temperature from the first sensor only.
  float tempC = OnewireSensors.getTempCByIndex(0);
  String mqttData = "{\"Dallastemperature\":";

  // Check if reading was successful
  if(tempC != DEVICE_DISCONNECTED_C) 
  {
    mqttSerial.printf("Temperature for the device 1 (index 0) is: %.2f",tempC);
    mqttData += tempC;
    mqttData += "}";
    client.publish(MQTT_BASE_TOPIC "/OneWire", mqttData.c_str());
    snprintf(jsonbuff + strlen(jsonbuff), MAX_MSG_SIZE - strlen(jsonbuff), "\"%s\":%.2f,", "Dallastemperature", tempC);
  } 
  else
  {
    mqttSerial.print("Error: Could not read temperature data");
  }
}
#endif

void setup()
{
  #ifdef ESP32
  Serial.begin(115200);
  setupScreen();
  MySerial.begin(9600, SERIAL_8E1, RX_PIN, TX_PIN);
  #elif ESP8266
  Serial.begin(9600, SERIAL_8E1);
  #endif
  pinMode(PIN_THERM, OUTPUT);

#ifdef PIN_SG1
  //Smartgrid pins
  pinMode(PIN_SG1, OUTPUT);
  pinMode(PIN_SG2, OUTPUT);
  digitalWrite(PIN_SG1, LOW);
  digitalWrite(PIN_SG2, LOW);
#endif
#ifdef ARDUINO_M5Stick_C_Plus
  gpio_pulldown_dis(GPIO_NUM_25);
  gpio_pullup_dis(GPIO_NUM_25);
#endif

  EEPROM.begin(10);
  readEEPROM();//Restore previous state
  mqttSerial.print("Setting up wifi...");
  setup_wifi();
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.onStart([]() {
    busy = true;
  });

  ArduinoOTA.onError([](ota_error_t error) {
    mqttSerial.print("Error on OTA - restarting");
    #ifdef ESTP32
    esp_restart();
    #elif ESP8266
    ESP.restart();
    #endif
  });
  ArduinoOTA.begin();

  client.setServer(MQTT_SERVER, MQTT_PORT);
  client.setBufferSize(MAX_MSG_SIZE); //to support large json message
  client.setCallback(callback);
  mqttSerial.print("Connecting to MQTT server...");
  mqttSerial.begin(&client, MQTT_LOG_TOPIC);
  reconnect();
  mqttSerial.println("OK!");

  initRegistries();
  mqttSerial.print("ESPAltherma started!");

  #ifdef ONEWIRE_BUS
  OnewireSensors.begin();
  mqttSerial.printf("Number of OneWire devices: %u",OnewireSensors.getDeviceCount());
  #endif  
}

void waitLoop(uint ms){
      unsigned long start = millis();
      while (millis() < start + ms) //wait .5sec between registries
      {
        extraLoop();
      }
}

void loop()
{
  if (!client.connected())
  { //(re)connect to MQTT if needed
    reconnect();
  }
  //Querying all registries
  for (size_t i = 0; (i < 32) && registryIDs[i] != 0xFF; i++)
  {
    char buff[64] = {0};
    int tries = 0;
    while (!queryRegistry(registryIDs[i], buff) && tries++ < 3)
    {
      mqttSerial.println("Retrying...");
      waitLoop(1000);
    }
    if (registryIDs[i] == buff[1]) //if replied registerID is coherent with the command
    {
      converter.readRegistryValues(buff); //process all values from the register
      updateValues(registryIDs[i]);       //send them in mqtt
      waitLoop(500);//wait .5sec between registries
    }
  }
  #ifdef ONEWIRE_BUS
  requestTemperatures();//OneWire bus request, before sendValues()!
  #endif  
  sendValues();//Send the full json message
  mqttSerial.printf("Done. Waiting %d sec...\n", FREQUENCY / 1000);
  waitLoop(FREQUENCY);
}
