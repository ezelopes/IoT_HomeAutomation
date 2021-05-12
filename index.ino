#include <ESP8266WiFi.h> // WiFi
#include <PubSubClient.h> // MQTT
#include <ESP8266HTTPClient.h> // HTTP
#include <time.h>
#include <sys/time.h>
#include <CronAlarms.h>
#include <SchedTask.h>

// WAKE UP ALARM FUNCTION
unsigned long previousMillis = 0;
long interval = 1000;
bool wakeUpAlarmTriggered = false;
int wakeUpLightStep = 20;
int maxWakeUpLightBrightness = 200;
char * minute = "16";
char * hour = "21";

// TIME SYNC
const int EPOCH_1_1_2019 = 1546300800;
const char* NTP_SERVER = "pool.ntp.org";
int myTimeZone = 0; // change this to your time zone
time_t now;

// ESP LED STATUS
bool motionLedIsOn = false;

// EXTERNAL LED
const int pinEXTERNAL_LED = 0;
int dc = 0;

// INFRARED MOTION SENSOR
const int pinINFRARED = 13; // 7;
int nonMotionDetectedCount = 0;
int nonMotionDetectedMax = 8000; // 8 seconds
int infraredSensorSteps = 100;

// BUTTON
const int pinBUTTON =  12; // 6;
bool isPushed = false;
String buttonStatus = "OFF"; // char*

// WIFI
WiFiClient espClient; // WiFiServer server(80);
const char * ssid = "My_Network_SSID";
const char * password = "********";

// ADAFRUIT
const char * MQTT_HOST = "io.adafruit.com";
const int MQTT_PORT = 1883;

const char * LED_LUMINOSITY_TOPIC = "ezelopes/feeds/LED-LUMINOSITY-FEED"; 
const char * PIR_TOPIC = "ezelopes/feeds/PIR-FEED"; 

String ledFeed = "LED-LUMINOSITY-FEED"; // Used for IF Statement check
String pirFeed = "PIR-FEED"; // Used for IF Statement check

const char * ADAFRUIT_IO_USERNAME = "ezelopes";
const char * ADAFRUIT_IO_KEY = "My_Adafruit_Key";

// SCHEDULER
void infraredFunction();
void publishFromModule();

SchedTask InfraredSensorTask (0, 100, infraredFunction);
SchedTask PublishingToBrokerTask (0, 100, publishFromModule);

//IFTTT
String key = "My_Webhook_Key"; //your webhooks key
String event_name = "wakeup_alarm"; //your webhooks event name
const char* FINGERPRINT = "Certificate_Thumbnail";

PubSubClient client(espClient);

boolean isNumeric(String str) {
  unsigned int stringLength = str.length();
  if (stringLength == 0) {
    return false;
  }
 
  boolean seenDecimal = false;
 
  for(unsigned int i = 0; i < stringLength; ++i) {
    if (isDigit(str.charAt(i))) {
      continue;
    }
 
    if (str.charAt(i) == '.') {
      if (seenDecimal) {
         return false;
       }
       seenDecimal = true;
       continue;
     }
     return false;
  }
  return true;
}

void resetExternalLedValues(){
  buttonStatus = "OFF";
  digitalWrite(LED_BUILTIN, HIGH);
  nonMotionDetectedCount = 0;
  motionLedIsOn = false;
}

void turnOnWakeUpLight(){
  if (wakeUpAlarmTriggered && (millis() - previousMillis >= interval)) {
    while (dc < maxWakeUpLightBrightness) {
      // Increasing brightness incrementally
      dc += wakeUpLightStep;
      analogWrite(pinEXTERNAL_LED, dc);
      client.publish(LED_LUMINOSITY_TOPIC, String(dc).c_str());

      Serial.println(dc);
      break;
    }
    previousMillis = millis();

    if (dc >= maxWakeUpLightBrightness) {
      wakeUpAlarmTriggered = false;
      Serial.println("WakeUpLight is fully on");
    }
  }
}

void MorningAlarm() {
  Serial.println("Turning music on");

  HTTPClient http;
  Serial.print("[HTTP] begin...\n");
  String webhookEndpoint = "https://maker.ifttt.com/trigger/" + event_name + "/with/key/" + key;
  http.begin(webhookEndpoint, FINGERPRINT);
  Serial.print("[HTTP] GET...\n");
  int httpCode = http.GET();
  Serial.println(httpCode);
  if (httpCode == 200){
    dc = 0;
    wakeUpAlarmTriggered = true;
    turnOnWakeUpLight();
  } else {
    Serial.println("Request failed");
  }
  http.end();
}

void publishFromModule(){
  int buttonRead = digitalRead(pinBUTTON); // 0 or 1

  if (buttonRead == 1 && !isPushed) {
    Serial.println("Button Pushed");
    isPushed = true;

    if (buttonStatus == "OFF") buttonStatus = "ON";
    else resetExternalLedValues(); // Stop reading from Infrared Sensor

    // Publish
    bool published = client.publish(PIR_TOPIC, buttonStatus.c_str());
    Serial.print("Published correctly: "); Serial.println(published);
  } else {
    isPushed = false;
  }
}

void infraredFunction(){
  if (buttonStatus == "ON") {
    int infrearedStatus = digitalRead(pinINFRARED); // 0 or 1

    if (infrearedStatus == 1) {
      // Turned on light, reset timer and set boolean variable LIGHT_ON to true.
      if (motionLedIsOn == false) Serial.println("Motion Detected. Light is on");

      digitalWrite(LED_BUILTIN, LOW);
      motionLedIsOn = true;
      nonMotionDetectedCount = 0;
    } else if (nonMotionDetectedCount >= nonMotionDetectedMax) {
      nonMotionDetectedCount = 0;
      digitalWrite(LED_BUILTIN, HIGH);
      motionLedIsOn = false;
    } else if (motionLedIsOn == true) {
      // Increase non-motion detection time by 100 (or STEPS variable). 
      // If variable reaches Max secs, then turn off light and set count to 0
      nonMotionDetectedCount += infraredSensorSteps;

      if (nonMotionDetectedCount % 1000 == 0) Serial.println(nonMotionDetectedCount); // Printing count every 1 second
      if (nonMotionDetectedCount == nonMotionDetectedMax) Serial.println("No motion detected. Turning light off!");
    }
  }
}

String splitString(char* e) {
  char* v[3];
  char *p;
  int i = 0;
  p = strtok(e, "/");
  while(p && i < 3)
   {
    v[i] = p;
    p = strtok(NULL, "/");
    i++;
  };

  // After the second forward slash, we find the Feed name.
  return String(v[2]);
 };

void handleMessageFromBroker(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived in topic: "); 
  Serial.println(topic);

  String data;
  for (int i = 0; i < length; i++) {
    data += (char)payload[i];
  }

  if (data == NULL) return;

  String feed = splitString(topic); // LED-LUMINOSITY-FEED or PIR-FEED 
  Serial.println(feed);

  if (feed == ledFeed){
    if (!isNumeric(data)) return;
    dc = data.toInt();
    analogWrite(pinEXTERNAL_LED, dc);
    Serial.print("PWM: "); Serial.println(data);
    return;
  }
  else if (feed == pirFeed) {
    if (data == "ON") buttonStatus = "ON";
    else if (data == "OFF") resetExternalLedValues(); // Stop reading from Infrared Sensor

    Serial.print("Button Status: "); Serial.println(data);
    return;
  }
  else Serial.print("Unkown Data: "); Serial.println(data);  
}

void setupWiFI(){
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println(WiFi.localIP());
}

void connectMQTTClient(){
  while (!client.connected()) {
    Serial.println("Connecting to MQTT...");
    String clientId = "ESP8266Client-" + String(random(0xffff), HEX);
    if (client.connect(clientId.c_str(), ADAFRUIT_IO_USERNAME, ADAFRUIT_IO_KEY )) {
      Serial.println("MQTT Broker Connected Successfully"); 
 
      if (client.subscribe(LED_LUMINOSITY_TOPIC, 1) && client.subscribe(PIR_TOPIC, 1)) Serial.println("Subscribed Successfully");
      else Serial.println("Failed with Subscription");

    } else {
      Serial.println("Failed with MQTT Connection");
      delay(2000);
    }
  }
}

void syncTime(){
  configTime(myTimeZone, 0, "0.uk.pool.ntp.org", "time.nist.gov");
  setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0", 1);

  while (now < EPOCH_1_1_2019) {
    now = time(nullptr);
    delay(500);
    Serial.print("*");
  }
  struct tm *tm_newtime;
  time(&now);
  tm_newtime = localtime(&now);

  int year = tm_newtime->tm_year;
  int month = tm_newtime->tm_mon + 1;
  int day = tm_newtime->tm_mday;
  int hour = tm_newtime->tm_hour;
  int minute = tm_newtime->tm_min;
  int second = tm_newtime->tm_sec;
  
  Serial.println();
  Serial.println(String(day) + "/" + String(month) + "/" + String(year) + " " + String(hour) + ":" + String(minute) + ":" + String(second));
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  delay(1000);

  client.setServer(MQTT_HOST, MQTT_PORT);
  client.setCallback(handleMessageFromBroker);

  setupWiFI();
  syncTime();
  connectMQTTClient();

  char buf[30]; // Variable used for concatenation

  strcpy(buf, "0 "); // Seconds
  strcat(buf, minute);
  strcat(buf, " ");
  strcat(buf, hour);
  strcat(buf, " * * *"); // Default as the task is on a daily basis
  
  Cron.create(buf, MorningAlarm, false); // Wake Up Alarm

  // Setup Pins
  pinMode(LED_BUILTIN, OUTPUT); // ESP LED
  pinMode(pinINFRARED, INPUT);
  pinMode(pinBUTTON, INPUT);
  analogWrite(pinEXTERNAL_LED, dc);

  digitalWrite(LED_BUILTIN, HIGH);

}

void loop() {
  struct tm *timeinfo;
  time(&now);
  timeinfo = localtime(&now);

  // Check if WiFi is disconnected 
  if(WiFi.status() == WL_DISCONNECTED){
    Serial.println("WiFi Connection lost. Reconnecting ...");
    setupWiFI();
  }

  // Check if MQTT is not connected 
  if(!client.connected()){
    Serial.println("MQTT Connection lost. Reconnecting ...");
    connectMQTTClient();  
  }

  client.loop();
  SchedBase::dispatcher();

  turnOnWakeUpLight();
  Cron.delay(100);
}
