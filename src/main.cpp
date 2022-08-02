/**
 * ----------------------------------------------------------------------------
 * ESP32 Remote Control with WebSocket
 * ----------------------------------------------------------------------------
 * © 2020 Stéphane Calderoni
 * ----------------------------------------------------------------------------
 */

#include <Arduino.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

// ----------------------------------------------------------------------------
// Definition of macros
// ----------------------------------------------------------------------------

#define LED_PIN   26
#define BTN_PIN   22
#define HTTP_PORT 80

// ----------------------------------------------------------------------------
// Definition of global constants
// ----------------------------------------------------------------------------

// Button debouncing
const uint8_t DEBOUNCE_DELAY = 10; // in milliseconds

// WiFi credentials
const char *WIFI_SSID = "Masters [2GHz]";
const char *WIFI_PASS = "0825631592";


//global Tank volume
double tankVolume;
double tankSize;

//global Tank height
String tankHeight;

//value for holding sensor reading
long distancevalue;

//global temp variable to check if reading changed

long tempDistanceReading;
bool tempDistanceReadingChanged;

//pin definitions for ultrasonic sensor

#define RELAY 21
#define RESERV_WIRE 36    //wire sensor pin to check if water in reserviour is still within threshold
#define TRIG_PIN 13
#define ECHO_PIN 12

bool ledState = 0;
const int ledPin = 2;


// ----------------------------------------------------------------------------
// Definition of the LED component
// ----------------------------------------------------------------------------

struct Led {
    // state variables
    uint8_t pin;
    bool    on;

    // methods
    void update() {
        digitalWrite(pin, on ? HIGH : LOW);
    }
};

// ----------------------------------------------------------------------------
// Definition of the Button component
// ----------------------------------------------------------------------------

struct Button {
    // state variables
    uint8_t  pin;
    bool     lastReading;
    uint32_t lastDebounceTime;
    uint16_t state;

    // methods determining the logical state of the button
    bool pressed()                { return state == 1; }
    bool released()               { return state == 0xffff; }
    bool held(uint16_t count = 0) { return state > 1 + count && state < 0xffff; }

    // method for reading the physical state of the button
    void read() {
        // reads the voltage on the pin connected to the button
        bool reading = digitalRead(pin);

        // if the logic level has changed since the last reading,
        // we reset the timer which counts down the necessary time
        // beyond which we can consider that the bouncing effect
        // has passed.
        if (reading != lastReading) {
            lastDebounceTime = millis();
        }

        // from the moment we're out of the bouncing phase
        // the actual status of the button can be determined
        if (millis() - lastDebounceTime > DEBOUNCE_DELAY) {
            // don't forget that the read pin is pulled-up
            bool pressed = reading == LOW;
            if (pressed) {
                     if (state  < 0xfffe) state++;
                else if (state == 0xfffe) state = 2;
            } else if (state) {
                state = state == 0xffff ? 0 : 0xffff;
            }
        }

        // finally, each new reading is saved
        lastReading = reading;
    }
};

// ----------------------------------------------------------------------------
// Definition of global variables
// ----------------------------------------------------------------------------

Led    onboard_led = { LED_BUILTIN, false };
Led    led         = { LED_PIN, false };
Button button      = { BTN_PIN, HIGH, 0, 0 };

AsyncWebServer server(HTTP_PORT);
AsyncWebSocket ws("/ws");




//GREENIOT FUNCTIONS //

String readFile(fs::FS &fs, const char * path){
  Serial.printf("Reading file: %s\r\n", path);
  File file = fs.open(path, "r");
  if(!file || file.isDirectory()){
    Serial.println("- empty file or failed to open file");
    return String();
  }
  Serial.println("- read from file:");
  String fileContent;
  while(file.available()){
    fileContent+=String((char)file.read());
  }
  file.close();
  Serial.println(fileContent);
  return fileContent;
}


void writeFile(fs::FS &fs, const char * path, const char * message){
  Serial.printf("Writing file: %s\r\n", path);
  File file = fs.open(path, "w");
  if(!file){
    Serial.println("- failed to open file for writing");
    return;
  }
  if(file.print(message)){
    Serial.println("- file written");
  } else {
    Serial.println("- write failed");
  }
  file.close();
}

//calculate distance of ultrasonic sensor
long calCulatedistance()
 {  
    long duration, distanceCm, distanceMm, result;
    
      // Give a short LOW pulse beforehand to ensure a clean HIGH pulse:
      digitalWrite(TRIG_PIN, LOW);
      delayMicroseconds(2u);
      digitalWrite(TRIG_PIN, HIGH);
      delayMicroseconds(10u);
      digitalWrite(TRIG_PIN, LOW);
      duration = pulseIn(ECHO_PIN,HIGH);
    
      // convert the time into a distance
      distanceCm = duration / 29.1 / 2 ;
      //   String distanceStr = String(distanceCm);
      // //write reading to JSON file readings.json
      //   File file = SPIFFS.open("/spiffs/readings.json", FILE_WRITE);
      //   StaticJsonDocument<256> doc;
      //   doc["Distance"] = distanceStr;
      //   serializeJson(doc,file);
      //   serializeJson(doc,Serial);
      //   file.close();
      //   //END Write reading to JSON file readings.json

if (distanceCm <= 0){
        Serial.println("Out of range");
      }
      else {
        // Serial.print(distanceIn);
        // Serial.print("in, ");


        Serial.print(distanceCm);
        Serial.print("cm");
        Serial.println();
        distanceMm = distanceCm * 10;
    tempDistanceReading = distanceCm;

        result=distanceMm;
      }
    delay(1000);


      return(result);
  }
//end calculate distance ultrasonic function

long calculateVolumeOfTank()
{
  double pi = 3.14;
  // 159265359;

  long heightlong = atol(readFile(SPIFFS, "/height.dat").c_str());
  long widthlong = atol(readFile(SPIFFS, "/width.dat").c_str());
  // double heightlong = 2255;
  // double widthlong = 1820;
  double tankRadius = widthlong/2;
  
  double calculatedVolume = (((tankRadius * tankRadius) * pi * heightlong)/1000)/1000;

  Serial.println(calculatedVolume);

  // String height = readFile(SPIFFS, "/data/height.dat");
  // String width = readFile(SPIFFS, "/data/width.dat");
  // String volume = readFile(SPIFFS, "/data/volume.dat");
  // String currentLevel = readFile(SPIFFS, "/data/currentlevel.dat");

// Serial.println("Calculated Volume of Tank :" + calculatedVolume);
return(calculatedVolume);

}


long calculateRemainingWater()
{

double pi = 3.14;

long heightlong = atol(readFile(SPIFFS, "/height.dat").c_str());
// long heightlong = atol(readFile(SPIFFS, "/tanksize.dat").c_str());

//adjust tank height according to manufacturer's FULL level.
heightlong = heightlong - 250;

long widthlong = atol(readFile(SPIFFS, "/width.dat").c_str());
double tankRadius = widthlong/2;

// double calculatedVolume = (((tankRadius * tankRadius) * pi * heightlong)/1000)/1000;

long remainHeight = heightlong - calCulatedistance();

double volume = (((tankRadius * tankRadius) * pi * remainHeight)/1000)/1000;
// double remain = calculateVolumeOfTank() - volume;
// Serial.println(remain);
// return(remain();


String waterLevelString = String(volume);
      // waterLevelFile.print(waterLevelString);
      writeFile(SPIFFS, "/currentlevel.dat", waterLevelString.c_str());
  

return(volume);
}

long calculateTankPercentage()
{
    long x = (calculateRemainingWater() / tankSize) * 100;

return (x);
}
//END GREEN IOT FUNCTIONS//






// ----------------------------------------------------------------------------
// SPIFFS initialization
// ----------------------------------------------------------------------------

void initSPIFFS() {
  if (!SPIFFS.begin()) {
    Serial.println("Cannot mount SPIFFS volume...");
    while (1) {
        onboard_led.on = millis() % 200 < 50;
        onboard_led.update();
    }
  }
}

// ----------------------------------------------------------------------------
// Connecting to the WiFi network
// ----------------------------------------------------------------------------

void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("Trying to connect [%s] ", WiFi.macAddress().c_str());
  while (WiFi.status() != WL_CONNECTED) {
      Serial.print(".");
      delay(500);
  }
  Serial.printf(" %s\n", WiFi.localIP().toString().c_str());
}

// ----------------------------------------------------------------------------
// Web server initialization
// ----------------------------------------------------------------------------

String processor(const String &var) 
{
    // return String(var == "STATE" && led.on ? "on" : "off");

    if (var == "percentRemain")
    {
            // return readFile(SPIFFS,"/currentlevel.dat");
            return String(calculateTankPercentage());
    }

    if (var == "litresRemain")
    {
    return String(calculateRemainingWater());

    }
}

void onRootRequest(AsyncWebServerRequest *request) {
  request->send(SPIFFS, "/index.html", "text/html", false, processor);
}

void initWebServer() {
    server.on("/", onRootRequest);
    server.serveStatic("/", SPIFFS, "/");
    server.begin();
}

// ----------------------------------------------------------------------------
// WebSocket initialization
// ----------------------------------------------------------------------------

void notifyClients() {
    const uint8_t size = JSON_OBJECT_SIZE(2);
    StaticJsonDocument<size> json;
    // json["status"] = led.on ? "on" : "off";

    json["remainingWater"] = calculateTankPercentage();
    json["litresRemain"] = calculateRemainingWater();

    char buffer[17];
    size_t len = serializeJson(json, buffer);
    ws.textAll(buffer, len);
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len) 
{
    AwsFrameInfo *info = (AwsFrameInfo*)arg;
    if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {

        const uint8_t size = JSON_OBJECT_SIZE(1);
        StaticJsonDocument<size> json;
        DeserializationError err = deserializeJson(json, data);
        if (err) {
            Serial.print(F("deserializeJson() failed with code "));
            Serial.println(err.c_str());
            return;
        }

        const char *action = json["action"];

        // if (strcmp(action, "toggle") == 0) {
        //     led.on = !led.on;
            notifyClients();
        }

}


void onEvent(AsyncWebSocket       *server,
             AsyncWebSocketClient *client,
             AwsEventType          type,
             void                 *arg,
             uint8_t              *data,
             size_t                len) {

    switch (type) {
        case WS_EVT_CONNECT:
            Serial.printf("WebSocket client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
            break;
        case WS_EVT_DISCONNECT:
            Serial.printf("WebSocket client #%u disconnected\n", client->id());
            break;
        case WS_EVT_DATA:
            handleWebSocketMessage(arg, data, len);
            break;
        case WS_EVT_PONG:
        case WS_EVT_ERROR:
            break;
    }
}

void initWebSocket() {
    ws.onEvent(onEvent);
    server.addHandler(&ws);
}

// ----------------------------------------------------------------------------
// Initialization
// ----------------------------------------------------------------------------

void setup() {

    Serial.begin(9600); delay(500);

    initSPIFFS();
    initWiFi();
    initWebSocket();
    initWebServer();


//ping modes for ultrasonic sensor
  pinMode(RELAY, OUTPUT);
  pinMode(TRIG_PIN,OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  digitalWrite(RELAY, LOW);


  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);   

  tankSize = atol(readFile(SPIFFS, "/tanksize.dat").c_str());
 

}

// ----------------------------------------------------------------------------
// Main control loop
// ----------------------------------------------------------------------------

void loop() 
{
    ws.cleanupClients();

    // button.read();

// if (tempDistanceReading == calCulatedistance())
// {
//     tempDistanceReadingChanged == false;
// }
// else
//     {
//     tempDistanceReadingChanged == true;
    

    calculateRemainingWater();
    // }

    // if (button.pressed()) {
    //     led.on = !led.on;
        notifyClients();
    // }
    
    // onboard_led.on = millis() % 1000 < 50;

    // led.update();
    // onboard_led.update();

// To access your stored values on inputString, inputInt, inputFloat

  String heightString = readFile(SPIFFS, "/height.dat");
  String widthString = readFile(SPIFFS, "/width.dat");
  String volumeString = readFile(SPIFFS, "/volume.dat");
  String currentLevelString = readFile(SPIFFS, "/currentlevel.dat");
  String tanksizestring = readFile(SPIFFS, "/tanksize.dat");

  Serial.println("---------------------------------");
  Serial.println("TankHeight : " + heightString);
  Serial.println("TankWidth : " + widthString);
  // Serial.println("TankVolume :" + volumeString);
  Serial.println("CurrentLevel :" + currentLevelString);
  Serial.println("Tank Size :" + tanksizestring);
  Serial.println("---------------------------------");


}