#include <ArduinoJson.h>
#include <ArduinoJson.hpp>

#include <WiFi.h>
#include <PubSubClient.h>

#include <string.h>

//Questa libreria serve per l'NTP
#include <time.h>

//Queste librerie servono per il termometro DS18B20
#include "OneWire.h"
#include "DallasTemperature.h"

#define TEMP_LED 4
//Configurazione termometro 
#define SENSOR_PIN 13
OneWire oneWire(SENSOR_PIN);
DallasTemperature tempSensor(&oneWire);


//Dati per la connessione WiFi
const char* ssid = "Gabriele-2.4GHz";
const char* pass = "i8Eo6zdPhvfqgzPVKo85hWP1";

//Dati per la configurazione MQTT
const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;

WiFiClient espClient;
PubSubClient client(espClient);

//Dati per la connessione al server NTP (per l'orario)
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600;
const int   daylightOffset_sec = 3600;


boolean active=true;
boolean observeTemperature= true;

// Il tick è la durata base di delayTime per il nostro ESP32
unsigned long tick=1000;

uint8_t tempTickCounter=0;
uint8_t tempTickQty=10; 

uint8_t connectionTickCounter=0;
uint8_t connectionTickQty=5;

/* Questo è il documento JSON che creiamo con tutti i valori ricevuti dai sensori.
 * Man mano che riceviamo valori, il documento cresce. Alla fine viene serializzato
 * ed inviato tramite MQTT
 */
DynamicJsonDocument jdoc(3600);
JsonArray array;

void setup() {

  Serial.begin(115200);
  setup_wifi();
  //setupNTP();
  setup_mqtt();
  // Init and get the time
  setupTime();
  setupWires();
}

/*
 * Questo serve per connettersi alla rete WiFi
 */
void setup_wifi(){

  delay(10);
  // We start by connecting to a WiFi network
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid,pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  randomSeed(micros());
  Serial.println("Connected to the WiFi network!!");
}

/*
 * Configurazione del broker MQTT.
 */
void setup_mqtt(){
  client.setServer(mqtt_server,mqtt_port);
  client.setCallback(callback);
  reconnect();
  delay(2000);
  StaticJsonDocument<200> resp;
  resp["v"]="rebooted";
  char buffer[128];
  size_t n = serializeJson(resp, buffer);
  checkConnection();
  client.publish("resp/br/10/3/0/4", buffer, n);
}

/*
 * Configurazione del server NTP per recuperare l'orario da internet
 */
void setupTime(){
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println(getTime());
}

void setupWires(){
  tempSensor.begin();
  pinMode(TEMP_LED,OUTPUT);
  digitalWrite(TEMP_LED,LOW);
}

void checkConnection(){
  if(!client.connected()){
    reconnect();
  }
}

void callback(char* topic, byte* message, unsigned int length) {
  Serial.println("Message arrived.");
  String messageTemp;
  
  //Qui convertiamo il byte* message in una stringa
  for (int i = 0; i < length; i++) {
    messageTemp += (char)message[i];
  }
  Serial.println(messageTemp);
  //Qui convertiamo il char* topic in un char[]
  char myTopic[50];
  char anotherTopic[50];
  strcpy(myTopic, topic);
  strcpy(anotherTopic, topic);

  //Qui tokenizziamo il token per navigarlo attraverso i vari levels
  String levels[10]; //Qui dentro avremo i nostri livelli
  char *ptr = NULL;
  byte index = 0;
  ptr = strtok(myTopic, "/");  // takes a list of delimiters
  while(ptr != NULL){
    levels[index] = ptr;
    index++;
    ptr = strtok(NULL, "/");  // takes a list of delimiters
  }
  String objectId=levels[3];
  String objectInstance=levels[4];
  String resId=levels[5];
  String observe=levels[6];

  StaticJsonDocument<200> doc;
  StaticJsonDocument<200> resp;

  if(objectId=="3303"){
    //Temperature
    if(objectInstance=="0"){
      if(resId=="5700"){
        //we want to take the value
        if(observe=="observe"){
          //we want to observe it
          // Deserialize the JSON document
          DeserializationError error = deserializeJson(doc, messageTemp);
          // Test if parsing succeeds.
          if (error) {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.f_str());
            return;
          }
          String value = doc["v"];
          resp["type"]="observe";
          if(value == "ON"){
            resp["v"]="ON";
            observeTemperature= true;
          }
          else if(value == "OFF"){
            resp["v"]="OFF";
            observeTemperature = false;
          }
          else return;
          char buffer[128];
          size_t n = serializeJson(resp, buffer);
          checkConnection();
          client.publish("resp/br/10/3303/0/5700", buffer, n);
          Serial.println(anotherTopic);
          client.publish(anotherTopic, NULL);
        }
        else processTemperature();
      }
    }
  }
  else if(objectId=="3311"){
    //LEDs
    Serial.println("Checking LEDS");
    // Deserialize the JSON document
    DeserializationError error = deserializeJson(doc, messageTemp);
    // Test if parsing succeeds.
      if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
      }
    String value = doc["v"];
    if(objectInstance=="0"){
      //tempLED
      if(resId=="5850"){
        if(value=="ON"){
          digitalWrite(TEMP_LED, HIGH);
        }
        else if(value=="OFF"){
          digitalWrite(TEMP_LED, LOW);
        }
      }
    }
  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    String clientId = "ESP32-Stocker-0";
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      delay(1000);
      subscribe();
    }
    else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  } 
}

void subscribe(){
  //With the # wildcard we subscribe to all the subtopics of each sublevel. 
  boolean res = client.subscribe("cmd/br/10/#");
  if(res){
    Serial.println("Subscribed!");
  }
  else{
    Serial.println("Unable to subscribe");
  }
}

void loop() {
  // put your main code here, to run repeatedly:
  client.loop();
  connectionTickCounter++;
  if(connectionTickCounter>=connectionTickQty){
    checkConnection();
    connectionTickCounter = 0;
  }
  
  if(active){
    //Serial.println("Active");
    if(observeTemperature){
      //Qui dentro entriamo se l'observe per la temp è attivo
      tempTickCounter++;
      if(tempTickCounter>=tempTickQty){
        //Time to read and send a new temperature data.
        processTemperature();
        tempTickCounter=0;
      }
    }
  }
  delay(tick);
}

void processTemperature(){
  Serial.println("Sending temperature..");
  tempSensor.requestTemperatures(); 
  float temperatureC = tempSensor.getTempCByIndex(0);
  char tempString[8];
  dtostrf(temperatureC, 1, 2, tempString);
  StaticJsonDocument<200> doc;
  doc["tstamp"]=getTime();
  doc["v"]=temperatureC;
  char buffer[128];
  size_t n = serializeJson(doc, buffer);
  checkConnection();
  Serial.print("Temperature: ");
  Serial.println(temperatureC);
  client.publish("data/br/10/3303/0/5700", buffer, n);
}

unsigned long getTime() {
  time_t now;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    //Serial.println("Failed to obtain time");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    return(0);
  }
  time(&now);
  return now;
}
