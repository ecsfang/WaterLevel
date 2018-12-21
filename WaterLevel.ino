/***************************************************************************
  WaterLevel - detect the water level in a bowl etc
 ***************************************************************************/
#include <FS.h>

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Ticker.h>

#ifdef USE_WEB_SERVER
//needed for library
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <WiFiManager.h>
#endif

#include "mySSID.h"
#include "VegetronixVH400.h"
#include "Pushover.h"

//#define USE_MQTT

#ifdef USE_WEB_SERVER
const char* CONFIG_FILE = "/config.json";
#endif

/**
 * Variables
 */
 
#ifdef USE_WEB_SERVER
// Indicates whether ESP has WiFi credentials saved from previous session
bool initialConfig = false;
#endif

VegetronixVH400 vh400(A0);

WiFiClient espClient;
PubSubClient client(espClient);

Ticker flipper;
Ticker pulse;

enum {
  START_MSG,
  WARN_MSG,
  ERR_MSG,
  OK_MSG
};

#define MSG_LEN 50
char msg[MSG_LEN];

unsigned long delayTime = 5000;

unsigned int warnLvl = 50;
unsigned int errLvl  = 10;

#define OK_LED      D4
#define ERROR_LED   D3

/**
 * Function Prototypes
 */

void pushMessage(int msg);
void flip(void);

#ifdef USE_WEB_SERVER
bool readConfigFile();
bool writeConfigFile();
//flag for saving data
bool shouldSaveConfig = false;

//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}
char  warn_level[10];
char  err_level[10];
#endif


void setup() {
  Serial.begin(115200);

  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(ERROR_LED, OUTPUT);

  for(int i=0; i<16; i++) {
    digitalWrite(OK_LED, HIGH);     // On ...
    digitalWrite(ERROR_LED, LOW);   // Off ...
    delay(100);
    digitalWrite(OK_LED, LOW);      // Off ...
    digitalWrite(ERROR_LED, HIGH);  // On ...
    delay(100);
  }

  digitalWrite(OK_LED, LOW);      // Off ...
  digitalWrite(ERROR_LED, LOW);   // Off ...

  Serial.println(F("WaterLevel!"));

#ifdef USE_WEB_SERVER
  readConfigFile();

  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_warn_level("Warning", "Warning level", warn_level, 8);
  WiFiManagerParameter custom_err_level("Error", "Error level", err_level, 8);

  flipper.attach(2.0, flip);
  
  unsigned long startedAt = millis();
  WiFi.printDiag(Serial); //Remove this line if you do not want to see WiFi password printed
  Serial.println("Opening configuration portal");
  digitalWrite(LED_BUILTIN, LOW); // turn the LED on by making the voltage LOW to tell us we are in configuration mode.
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager; 

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //add all your parameters here
  sprintf(warn_level, "%d", warnLvl);
  wifiManager.addParameter(&custom_warn_level);
  sprintf(err_level, "%d", errLvl);
  wifiManager.addParameter(&custom_err_level);

  //sets timeout in seconds until configuration portal gets turned off.
  //If not specified device will remain in configuration mode until
  //switched off via webserver.
  if (WiFi.SSID()!="") wifiManager.setConfigPortalTimeout(60); //If no access point name has been previously entered disable timeout.
  
  //it starts an access point 
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.startConfigPortal("ChristmasTree","christmaslight")) {//Delete these two parameters if you do not want a WiFi password on your configuration access point
     Serial.println("Not connected to WiFi but continuing anyway.");
  } else {
     //if you get here you have connected to the WiFi
     Serial.println("connected...yeey :)");
  }

  flipper.detach();
  digitalWrite(LED_BUILTIN, HIGH); // Turn led off as we are not in configuration mode.

  // For some unknown reason webserver can only be started once per boot up 
  // so webserver can not be used again in the sketch.
    
  Serial.print("After waiting ");
  int connRes = WiFi.waitForConnectResult();
  float waited = (millis()- startedAt);
  Serial.print(waited/1000);
  Serial.print(" secs in setup() connection result is ");
  Serial.println(connRes);
  if (WiFi.status()!=WL_CONNECTED){
    Serial.println("failed to connect, finishing setup anyway");
  } else{
    Serial.println("Ready");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  }
  if( shouldSaveConfig )
    writeConfigFile();

#else

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
//  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
//    delay(5000);
//    ESP.restart();
  } else
    Serial.println(WiFi.localIP());

#endif

  Serial.println();

  if( WiFi.status() == WL_CONNECTED ) {

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
  }
  
#ifdef USE_MQTT
  client.setServer(mqtt_server, 1883);
#endif

  pulse.attach(5.0, puls);

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

unsigned long reportMillis = 0;
unsigned long warnMillis = 0;
unsigned long errMillis = 0;
unsigned long warnRepeat = 0;
unsigned long errRepeat = 0;
unsigned long now;
bool          bOk = false;

#define SECONDS               1000
#define MINUTES               (60*SECONDS)
#define HOURS                 (60*MINUTES)
#define REPORT_REPEAT_TIME    (5*MINUTES)
#define WARNING_REPEAT_TIME   (3*HOURS)
#define ERROR_REPEAT_TIME     (1*HOURS)

double level = 0;

void loop()
{
#ifdef USE_MQTT
  if (!client.connected())
    reconnect();
#endif

  if (client.connected()) {
    client.loop();
    ArduinoOTA.handle();
  }
  
  level = vh400.getVWC();

  Serial.print("Level: ");
  Serial.println( level );

  now = millis();

  Serial.print("  Error @ ");
  Serial.print(errLvl);
  Serial.print(" : ");
  Serial.print((now-errMillis));
  Serial.print(" - ");
  Serial.println(errRepeat);
  Serial.print("  Warning @ ");
  Serial.print(warnLvl);
  Serial.print(" : ");
  Serial.print((now-warnMillis));
  Serial.print(" - ");
  Serial.println(warnRepeat);

  if( level < errLvl ) {
    if( (now-errMillis) > errRepeat ) {
      pushMessage(ERR_MSG);
      errMillis = now;
      errRepeat = ERROR_REPEAT_TIME;
      warnRepeat = 5000;
      bOk = false;
      flip();
    }
  } else if( level < warnLvl ) {
    if( (now-warnMillis) > warnRepeat ) {
      pushMessage(WARN_MSG);
      warnMillis = now;
      warnRepeat = WARNING_REPEAT_TIME;
      errRepeat = 5000;
      bOk = false;
      flip();
    }
  } else {
    //Enough water ...
    warnRepeat = errRepeat = 5000;
    if (!bOk )
      pushMessage(OK_MSG);
    bOk = true;
    flipper.detach();
    digitalWrite(ERROR_LED, LOW);  // Off ...
  }
  
  if( (now-reportMillis) > REPORT_REPEAT_TIME ) {
    sendMsgF("level", level);
    reportMillis = now;
  }
  
  delay(delayTime);
}

void pushMessage(int msg)
{
  char buf[64];

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
    case OK_MSG:
      sprintf(buf, "Waterlevel is ok!");
      break;
  }
  po.setMessage(buf);

  Serial.print("Send pushmessage: ");
  Serial.println(buf);

//  return; // ################################
  
  if (client.connected()) {
    Serial.println(po.send()); //should return 1 on success
  }
}

void pushWarning(void)
{
  if (client.connected()) {
    Pushover po = Pushover(pushAppToken,pushUserToken);
    po.setDevice("Xperia_XZ3");
    po.setMessage("Low on water!");
    po.setSound("siren");
    Serial.println(po.send()); //should return 1 on success
  }
}

void sendMsg(const char *topic, const char *m)
{
  digitalWrite(OK_LED, HIGH);     // On ...
  Serial.print("Publish message: ");
  snprintf (msg, MSG_LEN, "waterlevel/%s", topic);
  Serial.print(msg);
  Serial.print(" ");
  Serial.println(m);
#ifdef USE_MQTT
  client.publish(msg, m);
#endif
  digitalWrite(OK_LED, LOW);     // Off ...
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

#ifdef USE_WEB_SERVER
bool readConfigFile() {
  // this opens the config file in read-mode
  File file = SPIFFS.open(CONFIG_FILE, "r");

  if (!file) {
    Serial.println("Configuration file not found");
    return false;
  } else {
    // Allocate the document on the stack.
    // Don't forget to change the capacity to match your requirements.
    // Use arduinojson.org/assistant to compute the capacity.
    StaticJsonDocument<512> doc;
  
    // Deserialize the JSON document
    DeserializationError error = deserializeJson(doc, file);
    if (error)
      Serial.println(F("Failed to read file, using default configuration"));
  
    // Get the root object in the document
    JsonObject root = doc.as<JsonObject>();
  
    // Copy values from the JsonObject to the Config
    strcpy(warn_level, root["warnLvl"] | "50");
    strcpy(err_level,  root["errLvl"]  | "10");

    Serial.print("warnLvl: ");
    Serial.println(warn_level);
    Serial.print("errLvl: ");
    Serial.println(err_level);

    warnLvl = atoi(warn_level);
    errLvl = atoi(err_level);

    Serial.print("warnLvl: ");
    Serial.println(warnLvl);
    Serial.print("errLvl: ");
    Serial.println(errLvl);
    
    // Close the file (File's destructor doesn't close the file)
    file.close();
  }
  Serial.println("\nConfig file was successfully parsed");
  return true;
}

// Saves the configuration to a file
bool writeConfigFile() {
  Serial.println("Saving config file");

  // Open file for writing
  File file = SPIFFS.open(CONFIG_FILE, "w");
  if (!file) {
    Serial.println("Failed to open config file for writing");
    return false;
  }

  // Allocate the document on the stack.
  // Don't forget to change the capacity to match your requirements.
  // Use arduinojson.org/assistant to compute the capacity.
  StaticJsonDocument<256> doc;

  // Make our document contain an object
  JsonObject root = doc.to<JsonObject>();

  // Set the values in the object
  root["warnLvl"] = warn_level;
  root["errLvl"] = err_level;

  warnLvl = atoi(warn_level);
  errLvl = atoi(err_level);

  Serial.print("warnLvl: ");
  Serial.println(warnLvl);
  Serial.print("errLvl: ");
  Serial.println(errLvl);

  // Serialize JSON to file
  if (serializeJson(doc, file) == 0) {
    Serial.println(F("Failed to write to file"));
  }

  // Close the file (File's destructor doesn't close the file)
  file.close();
  return true;
}
#endif

void puls() {
  int state = digitalRead(OK_LED);  // get the current state of GPIO1 pin
  digitalWrite(OK_LED, !state);     // set pin to the opposite state

  if (state) {
    pulse.attach(5.0, puls);
  } else {
    pulse.attach(0.1, puls);
  }
}

void flip() {
  int state = digitalRead(ERROR_LED);  // get the current state of GPIO1 pin
  digitalWrite(ERROR_LED, !state);     // set pin to the opposite state

  // when the counter reaches a certain value, start blinking like crazy
  if (errLvl >= level) {
    flipper.attach(0.1, flip);
  } else if (warnLvl >= level) {
    flipper.attach(1.0, flip);
  } else {
    flipper.detach();
  }
}
