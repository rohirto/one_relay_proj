#include <stdio.h>
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h> 
#include <WiFiManager.h> 
#include <ArduinoOTA.h>
#include <WebSocketsClient.h>
#include <DNSServer.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include "u_macros.h"
#ifdef RASP_MQTT
#include <PubSubClient.h>
#else
//#include <UbidotsESPMQTT.h>
#include <PubSubClient.h>
#endif
#include <GDBStub.h>

#ifdef RASP_MQTT    /* If localhost or else Ubidots */
const char* mqtt_server ="192.168.1.149";     //Static raspberry Server
WiFiClient espClient;
PubSubClient client(espClient);
#else
//Ubidots client(TOKEN);
char* client_name = MQTT_CLIENT_NAME;
const char* mqtt_server ="industrial.api.ubidots.com";
char payload[700];
char topic[150];
ESP8266WiFiMulti WiFiMulti;
WiFiClient ubidots;
PubSubClient client(ubidots);
#endif
bool connected  = false;

void callback(char*, byte*, unsigned int);  //MQTT callback func
void reconnect(void);     //reconnect to MQTT
void Relay_setup(void);
void mqtt_subscribe(void);

unsigned long startMillis;  //Some global vaiable anywhere in program
unsigned long currentMillis;
volatile byte ten_sec_counter = 0;

//const byte D5 = 5;
const byte RELAY_PIN = 5;


void setup() {
  Serial.begin(115200);
  gdbstub_init();
  #if DEBUG
  pinMode(LED_BUILTIN, OUTPUT);     // Initialize the LED_BUILTIN pin as an output
  #endif
  Serial.println("Booting");

  WiFiManager wifiManager;
  wifiManager.setConfigPortalTimeout(180);
  wifiManager.autoConnect("Flop ESP", "espflopflop");
  //wifiManager.setSTAStaticIPConfig(IPAddress(192,168,1,150), IPAddress(192,168,1,1), IPAddress(255,255,255,0)); // optional DNS 4th argument
  //wifiManager.resetSettings();    //Uncomment to reset the Wifi Manager

  while (WiFi.status() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("My Room");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";
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
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });

  ArduinoOTA.begin();
  /**********************OTA Ends**********************************************************************/
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  /* MQTT Settings */
  #ifdef RASP_MQTT    //If localhost else Ubidots
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  SUBSCRIBE();
  #else
  //client.setClientName(client_name);
 client.setServer(mqtt_server, 1883);
 Serial.println("Server Set");
    client.setCallback(callback);
    Serial.println("Callback Set");
    mqtt_subscribe();
  //SUBSCRIBE();
  //Serial.println("Subscribe Set");
  #endif

  /* Basic Setup */
  Relay_setup();


  //  //Timer start
  startMillis = millis();

}

void loop()
{
     /* OTA stuff */
  ArduinoOTA.handle();
  /*****OTA Ends **************/

  /* Wifi Stuff */
   if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Reconnecting.");
    delay(5000);
    /* If Wifi is not connected, keep on the relay */
    //ON command
    //digitalWrite(RELAY_PIN, LOW);
    WiFi.reconnect();
  }

  connected = client.connected();
  if (!connected) {
    #ifdef RASP_MQTT
    reconnect();
    #else
    //client.reconnect();
    reconnect();
    mqtt_subscribe();
    #endif
  }
  client.loop();
  timer_function(); //Update Timers 
}

void callback(char* topic, byte* payload, unsigned int length)
{
    Serial.print("Message arrived [");
    Serial.print(topic);
    Serial.print("] ");
    char temp_buff[10];
    for (int i = 0; i<10; i++)
    {
        temp_buff[i] = '\0';
    }
    for( int i = 0; i < length; i++)
    {
        temp_buff[i] = (char)payload[i];
        Serial.print((char)payload[i]);  
    }
    Serial.println();
    float f_value = atof(temp_buff);
     if(strcmp(topic,RELAY_TOPIC) == 0)
    {
        if(f_value == 1)
        {
            //ON command
            digitalWrite(RELAY_PIN, LOW);
        }
        else
        {
            //Off command
            digitalWrite(RELAY_PIN, HIGH);
        }
    }
    else
    {
        //Wrong topic recieved
    }
}
#ifdef RASP_MQTT
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    if (client.connect("ESP8266Client_xyzsdjf")) {    //This MQTT CLient ID needs to be Unique
      Serial.println("connected");
      // Subscribe
      SUBSCRIBE();
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}
#else
void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.println("Attempting MQTT connection...");
    
    // Attempt to connect
    if (client.connect(MQTT_CLIENT_NAME, TOKEN,"")) {
      Serial.println("connected");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 2 seconds");
      // Wait 2 seconds before retrying
      delay(2000);
    }
  }
}
#endif
void Relay_setup()
{
  //define relay pins and their state
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH);
 
}

void timer_function()
{
  currentMillis = millis();  //get the current "time" (actually the number of milliseconds since the program started)
  if ( currentMillis - startMillis >= 10000)
  {
    startMillis = currentMillis;
    ten_sec_counter++;

   
    if ((ten_sec_counter % 60) == 0) //test whether the period has elapsed
    {
      //temp_humd_timer_elapsed = true;
      ten_sec_counter = 0;  //IMPORTANT to save the start time of the current LED state.
    }
  }

}
void mqtt_subscribe()
{
  //char *topicToSubscribe;
  sprintf(topic, "%s", RELAY_TOPIC);
  client.subscribe(topic);
}