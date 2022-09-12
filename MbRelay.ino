/************* Includes *************/
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include "ThingsBoard.h"          //https://github.com/thingsboard/thingsboard-arduino-sdk
#include <LittleFS.h>             //https://github.com/esp8266/Arduino/tree/master/libraries/LittleFS
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>    
//#include <AceButton.h>            // Button support
/************* End Includes *************/

/************* OTA Update *************/
#define CURRENT_FIRMWARE_TITLE    "MB-Relay-2-Ch"
#define CURRENT_FIRMWARE_VERSION  "1.0.0"
/************* End OTA *************/

/************* Double Reset config *************/
#define ESP_DRD_USE_LITTLEFS    true
#define ESP_DRD_USE_SPIFFS      false
#define ESP_DRD_USE_EEPROM      false
#define ESP8266_DRD_USE_RTC     false      
#define DOUBLERESETDETECTOR_DEBUG       true  //false
#include <ESP_DoubleResetDetector.h>    //https://github.com/khoih-prog/ESP_DoubleResetDetector
#define DRD_TIMEOUT 5 // Number of seconds after reset during which a subseqent reset will be considered a double reset.
#define DRD_ADDRESS 0

DoubleResetDetector* drd;
/************* End Double Reset config *************/

/************* Relay control *************/
//using namespace ace_button;
#define Relay1 5   // GPIO5-D1   morado
#define Relay2 4   // GPIO4-D2   naranja
#define Relay3 14  // GPIO14-D5   amarillo
#define Relay4 12  // GPIO12-D6    azul
int relayState = 10000;

//#define Switch1 0  // GPIO0-D3
//#define Switch2 13  // GPIO13-D7
//#define Switch3 15  // GPIO15-D8
//#define Switch4 16  // GPIO16-D0

// Helper macro to calculate array size
#define COUNT_OF(x) ((sizeof(x)/sizeof(0[x])) / ((size_t)(!(sizeof(x) % sizeof(0[x])))))

/*
ButtonConfig config1;
AceButton button1(&config1);
ButtonConfig config2;
AceButton button2(&config2);
ButtonConfig config3;
AceButton button3(&config3);
ButtonConfig config4;
AceButton button4(&config4);
*/
/************* End Relay control *************/

/************* Sensor DS18B20 *************/
const int oneWireBus = 2;     // GPIO2-D4
float temperature = 0.0;
OneWire oneWire(oneWireBus);          // Setup a oneWire instance to communicate with any OneWire devices
DallasTemperature sensors(&oneWire);  // Pass our oneWire reference to Dallas Temperature sensor
/************* End Sensor DS18B20 *************/

/************* Define default values *************/
#define SERIAL_DEBUG_BAUD   115200            // Baud rate for debug serial

// Thingsboard
char mqtt_server[40] = "thingsboard.cloud";
char mqtt_port[6] = "1883";
char api_token[34] = "";
char deviceid[32] = "";
byte macAddressArray[6];
/*
const char* provisionDeviceKey = "22j5rr8io3tfc9eahj1e";                     // Register (provision) device
const char* provisionDeviceSecret = "c23f4ex66bhbj25gaerp";
volatile bool provisionResponseProcessed = false;
*/
const char* deviceName = "MB-Relay-4-Ch";

//Wifi Manager
bool standAlone = false;
bool shouldSaveConfig = false;                //flag for saving data
int status = WL_IDLE_STATUS;                  // the Wifi radio's status
String deviceIP;

WiFiClient espClient;                         // Initialize Wifi
ThingsBoard tb(espClient);                    // Initialize Thingsboard instance

unsigned long lastTelemetryCheck;
unsigned long lastTelemetrySend;

//function prototypes
void readData();
void writeData();
void deleteData();
void saveConfigCallback();                                                  // callback notifying us of the need to save config
void getDeviceId(byte macAddressArray[], unsigned int len, char buffer[]);  // wifi macaddress, default length, char to save value
//void provisionDevice();                                                   // thingsboard - provision device if needed
//void claimDevice();                                                       // thingsboard - claim device if needed

/*
void handleEvent1(AceButton*, uint8_t, uint8_t);
void handleEvent2(AceButton*, uint8_t, uint8_t);
void handleEvent3(AceButton*, uint8_t, uint8_t);
void handleEvent4(AceButton*, uint8_t, uint8_t);
*/

// Processes function for thingsboard RPC call "setState"
RPC_Response processSetState(const RPC_Data &data){
  bool setState = false;
  int relay = data["relay"];
  switch (relay) {
    case 1:
      if (data["state"] == "on"){
       digitalWrite(Relay1, LOW);
       setState = true;
      } else {
          digitalWrite(Relay1, HIGH);
      }
      break;
    
    case 2:
      if (data["state"] == "on"){
       digitalWrite(Relay2, LOW);
       setState = true;
      } else {
          digitalWrite(Relay2, HIGH);
      }
      break;

    case 3:
      if (data["state"] == "on"){
       digitalWrite(Relay3, LOW);
       setState = true;
      } else {
          digitalWrite(Relay3, HIGH);
      }
      break;

    case 4:
      if (data["state"] == "on"){
       digitalWrite(Relay4, LOW);
       setState = true;
      } else {
          digitalWrite(Relay4, HIGH);
      }
      break;
  }

  //checkRelayState();    // Check RelayState and send telemetry
  return RPC_Response("setState", (setState ? 1 : 0));
}

// Processes function for thingsboard RPC call "getValue"
RPC_Response processGetState(const RPC_Data &data){
  checkRelayState();    // update global RelayStatus variable
  return RPC_Response("relayState", 1);
}

// Processes function for thingsboard RPC call "setGpioStatus"
RPC_Response processGetGpioStatus(const RPC_Data &data){
  Serial.println("Received the getGpioStatus method");
    
  return RPC_Response(NULL, (!digitalRead(Relay1) ? 1 : 0));
}


// RPC handlers
RPC_Callback callbacks[] = {
  {"getState", processGetState},
  {"setState", processSetState},
  {"getGpioStatus", processGetGpioStatus}
};


/************* End Define default values *************/

void setup() {
  Serial.begin(SERIAL_DEBUG_BAUD);

/************* Double Reset config *************/
//  pinMode(PIN_LED, OUTPUT); // initialize the LED digital pin as an output.
  drd = new DoubleResetDetector(DRD_TIMEOUT, DRD_ADDRESS);  
/************* End Double Reset config *************/

/************* Wifi manager *************/
  readData(); // Read data from config.json if exists
  WiFi.macAddress(macAddressArray);
  getDeviceId(macAddressArray, 6, deviceid); // get device id from macAddress

  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
  WiFiManagerParameter custom_api_token("apikey", "API token", api_token, 32);
  WiFiManagerParameter device_type("devicetype", "Tipo", deviceName, 40, " readonly");
  WiFiManagerParameter device_id("deviceid", "Device Id", deviceid, 40, " readonly");
  
  WiFiManager wifiManager;
  bool result;
  
  wifiManager.setSaveConfigCallback(saveConfigCallback); //set config save notify callback

  // custom parameters here
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_api_token);
  wifiManager.addParameter(&device_type);
  wifiManager.addParameter(&device_id);

  // Set captive portal menu
  //std::vector<const char *> menu = {"wifi","info","param","sep","restart","exit"};
  std::vector<const char *> menu = {"wifi","sep","exit"};
  wifiManager.setMenu(menu);

  //reset settings - for testing
  // wifiManager.resetSettings();
  // deleteData();

  //If double reset or first boot tries to connect to wifi
  //if it does not connect it starts an access point with the specified name
  //here  "MaterBox IoT"
  //and goes into a blocking loop awaiting configuration

  if (drd->detectDoubleReset()) {
    Serial.println("Double Reset Detected");

    wifiManager.setConfigPortalTimeout(180); // Time in seconds
    result = wifiManager.startConfigPortal("MaterBox IoT", "123456789");
    if (!result) {
      Serial.println("failed to connect and hit timeout. StandAlone mode ON");
      standAlone = true;
      saveConfigCallback ();
    } else {
        standAlone = false;
        saveConfigCallback ();
        Serial.println("MaterBox device is connected... :)");
    }
  } else {
    Serial.println("No Double Reset Detected");
      if (standAlone == false){
        wifiManager.setConfigPortalTimeout(180); // Time in seconds
        result = wifiManager.autoConnect("MaterBox IoT", "123456789");
        if (!result) {
          Serial.println("failed to connect and hit timeout");
          standAlone = true;
          saveConfigCallback ();
        } else {
            Serial.println("MaterBox device is connected... :)");
          }
      }
    }

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(api_token, custom_api_token.getValue());
  Serial.println("The values in the file are: ");
  Serial.println("\tmqtt_server : " + String(mqtt_server));
  Serial.println("\tmqtt_port : " + String(mqtt_port));
  Serial.println("\tapi_token : " + String(api_token));
  Serial.println("\tstand_alone : " + String(standAlone ? "true" : "false"));

  /*
  // If provision is needed
  if (api_token[0] == '\0'){
    Serial.println("Send provision device request");
    provisionDevice();
    Serial.println("Send claiming device request");
    claimDevice();
  }
  */
  
  if (shouldSaveConfig) {
    writeData();
  }

  if (!standAlone){
    Serial.println("local ip");
    Serial.println(WiFi.localIP());
    deviceIP = "IP:" + WiFi.localIP().toString();
  } else {
    deviceIP = String(deviceName);
  }

/************* Relay control *************/
  pinMode(Relay1, OUTPUT);
  pinMode(Relay2, OUTPUT);
  pinMode(Relay3, OUTPUT);
  pinMode(Relay4, OUTPUT);

//  pinMode(Switch1, INPUT_PULLUP);
//  pinMode(Switch2, INPUT_PULLUP);
//  pinMode(Switch3, INPUT_PULLUP);
//  pinMode(Switch4, INPUT_PULLUP);
  
  //During Starting all Relays should TURN OFF
  digitalWrite(Relay1, HIGH);
  digitalWrite(Relay2, HIGH);
  digitalWrite(Relay3, HIGH);
  digitalWrite(Relay4, HIGH);

//  config1.setEventHandler(button1Handler);
//  config2.setEventHandler(button2Handler);
//  config3.setEventHandler(button3Handler);
//  config4.setEventHandler(button4Handler);
  
//  button1.init(Switch1);
//  button2.init(Switch2);
//  button3.init((uint8_t)Switch3);
//  button4.init(Switch4);
/************* End Relay control *************/

  // Start the DS18B20 sensor
  sensors.begin();
  lastTelemetryCheck = millis() + 1100;   // we need check telemetry at the very begining
  lastTelemetrySend = millis() + 30100;   // we need send telemetry at the very begining
}

/************* End setup function *************/

void loop() {
  //button1.check();
  //button2.check();
  //button3.check();
  // button4.check();
  
  if ( millis() - lastTelemetryCheck > 1000 ) { // Update and check only after 1 second    
    /*  If needs to do something every second    */
    
    if ( millis() - lastTelemetrySend > 30000 ) { // Update and send only after 30 seconds
      if (!standAlone){
        if ((!tb.connected() && standAlone == false)) {
          reconnect();
        }

        checkRelayState();    // Check RelayState and send telemetry

        // OTA update
        if (tb.Firmware_Update(CURRENT_FIRMWARE_TITLE, CURRENT_FIRMWARE_VERSION)) {
          Serial.println("Done, Reboot now");
          ESP.restart();
        }
        else {
          Serial.println("No new firmware");
        }
      }
      lastTelemetrySend = millis();
    }
    lastTelemetryCheck = millis();
  }

  tb.loop();
  drd->loop();

}

void checkRelayState() {
  int checkRelay = 10000 + int(!digitalRead(Relay4) ? 1000 : 0) + int(!digitalRead(Relay3) ? 100 : 0) + int(!digitalRead(Relay2) ? 10 : 0) + int(!digitalRead(Relay1) ? 1 : 0);

  sensors.requestTemperatures(); 
  temperature = sensors.getTempCByIndex(0);  
  sendTelemetry(checkRelay, temperature);
    
}

void sendTelemetry(int checkRelay, float temperature){
  if (relayState == checkRelay){
    tb.sendTelemetryFloat("temperature", temperature);
    Serial.print("Temperature: ");
    Serial.println(temperature);
  } else {
      
      tb.sendTelemetryInt("relaystatus", checkRelay);
      relayState = checkRelay;
      tb.sendTelemetryFloat("temperature", temperature);
      
      Serial.print("Relay state: ");
      Serial.println(relayState);
      Serial.print("Temperature: ");
      Serial.println(temperature);
    }
}

void UpdatedCallback(const bool& success) {
  if (success) {
    Serial.println("Done, Reboot now");
    ESP.restart();
  }
  else {
    Serial.println("No new firmware");
  }
}

void readData() {
    //https://www.hackster.io/Neutrino-1/littlefs-read-write-delete-using-esp8266-and-arduino-ide-867180
    //read configuration from FS config.json
  if(!LittleFS.begin()){
    Serial.println("An Error has occurred while mounting LittleFS");
    //Print the error on display
    Serial.println("Mounting Error");
    delay(1000);
    return;
  } else {
    Serial.println("mounted file system");
    if (LittleFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = LittleFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);

        DynamicJsonDocument json(1024);
        auto deserializeError = deserializeJson(json, buf.get());
        serializeJson(json, Serial);
        if (!deserializeError) {
          Serial.println("\nparsed json");
          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(api_token, json["api_token"]);
          standAlone = json["standAlone"];
        } else {
          Serial.println("failed to load json config");
        }
        configFile.close();
      }
    }
  }
}

void writeData() {
      Serial.println("saving config");
      DynamicJsonDocument json(1024);
      json["mqtt_server"] = mqtt_server;
      json["mqtt_port"] = mqtt_port;
      json["api_token"] = api_token;
      json["standAlone"] = standAlone;
      
      File configFile = LittleFS.open("/config.json", "w");
      if (!configFile) {
        Serial.println("failed to open config file for writing");
      }
      serializeJson(json, Serial);
      serializeJson(json, configFile);
      configFile.close();
      Serial.println();
}

void deleteData(){
   //Remove the file
   LittleFS.remove("/config.json");
}

// Save config.json
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

// https://stackoverflow.com/questions/44748740/convert-byte-array-in-hex-to-char-array-or-string-type-arduino
void getDeviceId(byte macAddressArray[], unsigned int len, char buffer[])
{
    for (unsigned int i = 0; i < len; i++)
    {
        byte nib1 = (macAddressArray[i] >> 4) & 0x0F;
        byte nib2 = (macAddressArray[i] >> 0) & 0x0F;
        buffer[i*2+0] = nib1  < 0xA ? '0' + nib1  : 'A' + nib1  - 0xA;
        buffer[i*2+1] = nib2  < 0xA ? '0' + nib2  : 'A' + nib2  - 0xA;
    }
    buffer[len*2] = '\0';
}

/*
void provisionDevice(){
  int port = String(mqtt_port).toInt();

  // Initialize ThingsBoard client provision instance
  ThingsBoard tb_provision(espClient);
  
  const Provision_Callback provisionCallback = processProvisionResponse;
  
  if (!tb_provision.connected()) {
    // Connect to the ThingsBoard
    Serial.print("Connecting to: ");
    Serial.print(mqtt_server);
    if (!tb_provision.connect(mqtt_server, "provision", port)) {
      Serial.println("Failed to connect");
      return;
    }
    if (tb_provision.Provision_Subscribe(provisionCallback)) {
      if (tb_provision.sendProvisionRequest(deviceName, provisionDeviceKey, provisionDeviceSecret)) {
        Serial.println("Provision request was sent!");
      }
    }
  }

  while (!provisionResponseProcessed){
      tb_provision.loop();
    }
  
  if (tb_provision.connected()) {
    tb_provision.disconnect();
  }
}

void processProvisionResponse(const Provision_Data &data){
  // Struct for client connecting after provisioning
  struct Credentials {
    String client_id;
    String username;
    String password;
  };
  Credentials credentials;
  
  Serial.println("Received device provision response");
  int jsonSize = measureJson(data) + 1;
  char buffer[jsonSize];
  serializeJson(data, buffer, jsonSize);
  Serial.println(buffer);
  if (strncmp(data["status"], "SUCCESS", strlen("SUCCESS")) != 0) {
    Serial.print("Provision response contains the error: ");
    Serial.println(data["errorMsg"].as<String>());
    provisionResponseProcessed = true;
    return;
  }
  if (strncmp(data["credentialsType"], "ACCESS_TOKEN", strlen("ACCESS_TOKEN")) == 0) {
    credentials.client_id = "";
    credentials.username = data["credentialsValue"].as<String>();
    credentials.password = "";
    credentials.username.toCharArray(api_token, 34);
  }
  if (strncmp(data["credentialsType"], "MQTT_BASIC", strlen("MQTT_BASIC")) == 0) {
    JsonObject credentials_value = data["credentialsValue"].as<JsonObject>();
    credentials.client_id = credentials_value["clientId"].as<String>();
    credentials.username = credentials_value["userName"].as<String>();
    credentials.password = credentials_value["password"].as<String>();
    credentials.client_id.toCharArray(api_token, 34);
  }
  writeData();
  provisionResponseProcessed = true;
}

void claimDevice(){
  // Initialize ThingsBoard client provision instance
  ThingsBoard tb_claiming(espClient);
  unsigned int claimingRequestDurationMs = 84600000;
  int port = String(mqtt_port).toInt();

  if (!tb_claiming.connected()) {
    // Connect to the ThingsBoard
    Serial.print("Connecting to: ");
    Serial.println(mqtt_server);
    if (!tb_claiming.connect(mqtt_server, api_token, port)) {
      Serial.println("Failed to connect");
      return;
    }
    if (tb_claiming.sendClaimingRequest(deviceid, claimingRequestDurationMs)) {
      Serial.println("claiming request sent");
    }
  }

  if (tb_claiming.connected()) {
    tb_claiming.disconnect();
  }
}
*/

void reconnect() {
  int port = String(mqtt_port).toInt();
  
  // Loop until we're reconnected
  while (!tb.connected()) {
    Serial.print("Connecting to ThingsBoard node ...");
    if (tb.connect(mqtt_server, api_token, port)) {
      Serial.println( "[DONE]" );
      Serial.print("Subscribing to RPC and OTA ...");
      (!tb.RPC_Subscribe(callbacks, COUNT_OF(callbacks)) ? Serial.println( "[FAILED]" ) : Serial.println( "[DONE]" ));
    } else {
      Serial.print( "[FAILED]" );
      Serial.println( " : retrying in 5 seconds]" );
      // Wait 5 seconds before retrying
      delay(5000);
      standAlone == true;
      break;
    }
  }
}

/*
void button1Handler(AceButton* button, uint8_t eventType, uint8_t buttonState) {
  switch (eventType) {
    case AceButton::kEventPressed:
      digitalWrite(Relay1, HIGH);
      Serial.println("All relays ON");
      break;
    
    case AceButton::kEventReleased:
      digitalWrite(Relay1, LOW);
      Serial.println("All relays OFF");      
      //relayOnOff(1);
      break;
  }
}

void button2Handler(AceButton* button, uint8_t eventType, uint8_t buttonState) {
  switch (eventType) {
    case AceButton::kEventReleased:
      //relayOnOff(2);
      break;
  }
}

void button3Handler(AceButton* button, uint8_t eventType, uint8_t buttonState) {
  switch (eventType) {
    case AceButton::kEventPressed:
      digitalWrite(Relay3, LOW);
      Serial.println("All relays ON");
      break;
    
    case AceButton::kEventReleased:
      digitalWrite(Relay3, HIGH);
      Serial.println("All relays OFF");      
      //relayOnOff(1);
      break;
  }
}

void button4Handler(AceButton* button, uint8_t eventType, uint8_t buttonState) {
  switch (eventType) {
    case AceButton::kEventReleased:
      //relayOnOff(4);
      break;
  }
}
*/
