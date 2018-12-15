/***************************************************************************
  WaterLevel - detect the water level in a bowl etc
 ***************************************************************************/

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>

#include "mySSID.h"
#include "VegetronixVH400.h"
#include "Pushover.h"

VegetronixVH400 vh400(A0);

WiFiClient espClient;
PubSubClient client(espClient);

enum {
  START_MSG,
  WARN_MSG,
  ERR_MSG
};

#define MSG_LEN 50
char msg[MSG_LEN];

unsigned long delayTime = 1000;

void pushMessage(int msg);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("WaterLevel!"));

  pinMode(LED_BUILTIN, OUTPUT);

  for(int i=0; i<16; i++) {
    digitalWrite(LED_BUILTIN, LOW);  // On ...
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);  // Off ...
    delay(100);
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  Serial.println();

  ArduinoOTA.setHostname("WaterLevel");
  ArduinoOTA.setPassword(flashpw);

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }
    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    } else {
      Serial.println("Unknown error!");
    }
  });
  ArduinoOTA.begin();

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  client.setServer(mqtt_server, 1883);

  pushMessage(START_MSG);
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
#ifdef RX_DEBUG
    Serial.print("Attempting another MQTT connection...");
#endif
    // Attempt to connect
    if (client.connect("theWaterLevel")) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("waterLevel", "ready");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

unsigned long warnMillis = 0;
unsigned long errMillis = 0;
unsigned long now;

#define LOW_WATER_LEVEL       50
#define VERY_LOW_WATER_LEVEL  10

#define WARNING_REPEAT_TIME   (60*60*1000)
#define ERROR_REPEAT_TIME     (10*60*1000)

void loop()
{
  if (!client.connected())
    reconnect();

  if (client.connected())
    client.loop();

  ArduinoOTA.handle();

  double w = vh400.getVWC();

  Serial.print("Level: ");
  Serial.print( w );

  now = millis();

  Serial.print("  -  ");
  Serial.print(warnMillis);
  Serial.print(" : ");
  Serial.println(errMillis);
  
  if( w < VERY_LOW_WATER_LEVEL && (now-errMillis) > ERROR_REPEAT_TIME ) {
    pushMessage(ERR_MSG);
    errMillis = now;
  } else if( w < LOW_WATER_LEVEL && (now-warnMillis) > WARNING_REPEAT_TIME ) {
    pushMessage(WARN_MSG);
    warnMillis = now;
  }
  
  delay(delayTime);
}

void pushMessage(int msg)
{
  char buf[64];

  Serial.print("Send pushmessage: ");
  Serial.println(msg);
  return;
  
  Pushover po = Pushover(pushAppToken,pushUserToken);
//  po.setDevice("Xperia_XZ3");
  switch(msg) {
    case START_MSG:
      sprintf(buf, "Meter is started!");
      po.setSound("bike");
      break;
    case WARN_MSG:
      sprintf(buf, "Waterlevel is low!");
      po.setSound("space alarm");
      break;
    case ERR_MSG:
      sprintf(buf, "Warning! Waterlevel is very low!!");
      po.setSound("siren");
      break;
  }
  po.setMessage(buf);
  Serial.println(po.send()); //should return 1 on success
}

void pushWarning(void)
{
  Pushover po = Pushover(pushAppToken,pushUserToken);
  po.setDevice("Xperia_XZ3");
  po.setMessage("Low on water!");
  po.setSound("siren");
  Serial.println(po.send()); //should return 1 on success
}

void sendMsg(const char *topic, const char *m)
{
  Serial.print("Publish message: ");
  snprintf (msg, MSG_LEN, "waterlevel/%s", topic);
  Serial.print(msg);
  Serial.print(" ");
  Serial.println(m);
  client.publish(msg, m);
}

void sendMsgF(const char *topic, double v)
{
  char buf[32];
  snprintf (buf, 32, "%.2f", v);
  sendMsg(topic, buf);
}
void sendMsgI(const char *topic, int v)
{
  char buf[32];
  snprintf (buf, 32, "%d", v);
  sendMsg(topic, buf);
}


