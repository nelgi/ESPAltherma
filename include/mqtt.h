#include <PubSubClient.h>
#include <EEPROM.h>
#define MQTT_attr MQTT_BASE_TOPIC "/ATTR"
#define MQTT_lwt MQTT_BASE_TOPIC "/LWT"
#define MQTT_state MQTT_BASE_TOPIC "/STATE"
#define MQTT_power MQTT_BASE_TOPIC "/POWER"

#define EEPROM_CHK 1
#define EEPROM_STATE 0

#ifdef JSONTABLE
char jsonbuff[MAX_MSG_SIZE] = "[{\0";
#else
char jsonbuff[MAX_MSG_SIZE] = "{\0";
#endif

WiFiClient espClient;
PubSubClient client(espClient);

void sendValues()
{
  #ifdef ESP32
  Serial.printf("Sending values in MQTT.\n");
  #endif
#ifdef ARDUINO_M5Stick_C
  //Add M5 APX values
  snprintf(jsonbuff + strlen(jsonbuff),MAX_MSG_SIZE - strlen(jsonbuff) , "\"%s\":\"%.3gV\",\"%s\":\"%gmA\",", "M5VIN", M5.Axp.GetVinVoltage(),"M5AmpIn", M5.Axp.GetVinCurrent());
  snprintf(jsonbuff + strlen(jsonbuff),MAX_MSG_SIZE - strlen(jsonbuff) , "\"%s\":\"%.3gV\",\"%s\":\"%gmA\",", "M5BatV", M5.Axp.GetBatVoltage(),"M5BatCur", M5.Axp.GetBatCurrent());
  snprintf(jsonbuff + strlen(jsonbuff),MAX_MSG_SIZE - strlen(jsonbuff) , "\"%s\":\"%.3gmW\",", "M5BatPwr", M5.Axp.GetBatPower());
#endif  

  jsonbuff[strlen(jsonbuff) - 1] = '}';
#ifdef JSONTABLE
  strcat(jsonbuff,"]");
#endif
  client.publish(MQTT_attr, jsonbuff);
#ifdef JSONTABLE
  strcpy(jsonbuff, "[{\0");
#else
  strcpy(jsonbuff, "{\0");
#endif
}

void saveEEPROM(uint8_t state){
    EEPROM.write(EEPROM_STATE,state);
    EEPROM.commit();
}

void readEEPROM(){
  if ('R' == EEPROM.read(EEPROM_CHK)){
    digitalWrite(PIN_THERM,EEPROM.read(EEPROM_STATE));
    mqttSerial.printf("Restoring previous state: %s",(EEPROM.read(EEPROM_STATE) == HIGH)? "Off":"On" );
  }
  else{
    mqttSerial.printf("EEPROM not initialized (%d). Initializing...",EEPROM.read(EEPROM_CHK));
    EEPROM.write(EEPROM_CHK,'R');
    EEPROM.write(EEPROM_STATE,HIGH);
    EEPROM.commit();
    digitalWrite(PIN_THERM,HIGH);
  }
}

String mqtt_topic(byte type){ // 0 - sensor, everything else switch
  String topic;
  topic.reserve(128);
  topic = "homeassistant/";
  if(type == 0) {
    topic += "sensor/";
  } else {
    topic += "switch/";
  }
  topic += MQTT_BASE_TOPIC;
  //topic += "-";
  //topic += mac_address(); // for added uniqueness (Tasmota style)
  topic += "/config";
  return topic;
}

String mqtt_payload(byte type){ // 0 - sensor, everything else switch
  String payload;
  payload.reserve(384);
  if(type == 0) {
    payload = "{\"name\":\"Altherma Sensors\",\"stat_t\":\"~/SENSOR\",\"json_attr_t\":\"~/ATTR\"";
  } else {
    payload = "{\"name\":\"Altherma\",\"cmd_t\":\"~/POWER\",\"stat_t\":\"~/STATE\",\"pl_off\":\"OFF\",\"pl_on\":\"ON\"";
  }
  payload += ",\"avty_t\":\"~/LWT\",\"pl_avail\":\"Online\",\"pl_not_avail\":\"Offline\",\"uniq_id\":\"";
  payload += mac_address(); // unique_id: AABBCC-sensor/switch
  if(type == 0) {
    payload += "_sensor";
  } else {
    payload += "_switch";
  }
  payload += "\",\"dev\":{\"ids\":[\"";
  payload += mac_address(); // device identifier
  payload += "\"]";
  if(type == 0){
    payload += ",\"name\":\"";
    payload += HOSTNAME; // name of the device in HA
    payload += "\"";
  }
  payload += "}, \"object_id\":\"";
  payload += MQTT_BASE_TOPIC; // sensor name in HA
  payload += "\", \"~\":\"";
  payload += MQTT_BASE_TOPIC; // MQTT base topic
  payload += "\"}";
  return payload;
}

void reconnect()
{
  // Loop until we're reconnected
  int i = 0;
  while (!client.connected())
  {
    #ifdef ESP32
    Serial.print("Attempting MQTT connection...");
    #endif

    if (client.connect(HOSTNAME, MQTT_USERNAME, MQTT_PASSWORD, MQTT_lwt, 0, true, "Offline"))
    {
      #ifdef ESP32
      Serial.println("connected!");
      #endif
      // Sensor
      client.publish(mqtt_topic(0).c_str(),mqtt_payload(0).c_str(),true); // (0) = sensor
      client.publish(MQTT_lwt, "Online", true);
      // Switch
      client.publish(mqtt_topic(1).c_str(),mqtt_payload(1).c_str(),true); // (1) = switch
      // Subscribe
      client.subscribe(MQTT_power);
#ifdef PIN_SG1
      client.publish("homeassistant/sg/espAltherma/config", "{\"name\":\"AlthermaSmartGrid\",\"cmd_t\":\"~/set\",\"stat_t\":\"~/state\",\"~\":\"espaltherma/sg\"}", true);
      client.subscribe("espaltherma/sg/set");
#endif
    }
    else
    {
      #ifdef ESP32
      Serial.printf("failed, rc=%d, try again in 5 seconds", client.state());
      #endif
      unsigned long start = millis();
      while (millis() < start + 5000)
      {
        ArduinoOTA.handle();
      }

      if (i++ == 100)
        #ifdef ESP32
        Serial.printf("Tried for 500 sec, rebooting now.");
        esp_restart();
        #elif ESP8266
        ESP.restart();
        #endif
    }
  }
}

void callbackTherm(byte *payload, unsigned int length)
{
  payload[length] = '\0';
  
  // Is it ON or OFF?
  // Ok I'm not super proud of this, but it works :p 
  if (payload[1] == 'F')
  { //turn off
    digitalWrite(PIN_THERM, HIGH);
    saveEEPROM(HIGH);
    client.publish(MQTT_state, "OFF");
    mqttSerial.println("Turned OFF");
  }
  else if (payload[1] == 'N')
  { //turn on
    digitalWrite(PIN_THERM, LOW);
    saveEEPROM(LOW);
    client.publish(MQTT_state, "ON");
    mqttSerial.println("Turned ON");
  }
  else if (payload[0] == 'R')//R(eset/eboot)
  { 
    mqttSerial.println("Rebooting");
    delay(100);
    #ifdef ESP32
    esp_restart();
    #elif ESP8266
    ESP.restart();
    #endif
  }  
  else
  {
    #ifdef ESP32
    Serial.printf("Unknown message: %s\n", payload);
    #endif
  }
}

#ifdef PIN_SG1
//Smartgrid callbacks
void callbackSg(byte *payload, unsigned int length)
{
  payload[length] = '\0';
  
  if (payload[0] == '0')
  {
    // Set SG 0 mode => SG1 = LOW, SG2 = LOW
    digitalWrite(PIN_SG1, LOW);
    digitalWrite(PIN_SG2, LOW);
    client.publish("espaltherma/sg/state", "0");
    Serial.println("Set SG mode to 0 - Normal operation");
  }
  else if (payload[0] == '1')
  {
    // Set SG 1 mode => SG1 = LOW, SG2 = HIGH
    digitalWrite(PIN_SG1, LOW);
    digitalWrite(PIN_SG2, HIGH);
    client.publish("espaltherma/sg/state", "1");
    Serial.println("Set SG mode to 1 - Forced OFF");
  }
  else if (payload[0] == '2')
  {
    // Set SG 2 mode => SG1 = HIGH, SG2 = LOW
    digitalWrite(PIN_SG1, HIGH);
    digitalWrite(PIN_SG2, LOW);
    client.publish("espaltherma/sg/state", "2");
    Serial.println("Set SG mode to 2 - Recommended ON");
  }
  else if (payload[0] == '3')
  {
    // Set SG 3 mode => SG1 = HIGH, SG2 = HIGH
    digitalWrite(PIN_SG1, HIGH);
    digitalWrite(PIN_SG2, HIGH);
    client.publish("espaltherma/sg/state", "3");
    Serial.println("Set SG mode to 3 - Forced ON");
  }
  else
  {
    Serial.printf("Unknown message: %s\n", payload);
  }
}
#endif

void callback(char *topic, byte *payload, unsigned int length)
{
  #ifdef ESP32
  Serial.printf("Message arrived [%s] : %s\n", topic, payload);
  #endif

  if (strcmp(topic, MQTT_power) == 0)
  {
    callbackTherm(payload, length);
  }
#ifdef PIN_SG1
  else if (strcmp(topic, "espaltherma/sg/set") == 0)
  {
    callbackSg(payload, length);
  }
#endif
  else
  {
    #ifdef ESP32
    Serial.printf("Unknown topic: %s\n", topic);
    #endif
  }
}