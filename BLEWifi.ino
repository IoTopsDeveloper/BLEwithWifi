// Default Arduino includes
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <ArduinoJson.h>
#include <dht.h>
#define dht_dpin 18  //no ; here. Set equal to channel sensor is on
dht DHT;
#include <BLEUtils.h>
#include <BLEServer.h>
#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include <BLE2902.h>
#include <Preferences.h>
int LEDBuiltInConnected=4;
int LEDBLEMode=22;
int LEDWifiNotConnected=23;
int Button1=32;
int Button2=35;
int LED1 = 19;
int LED2 = 21;
char previous_state[4];
char globalJson[200]; 
char wifi_status[30];
char Nill[150];
WiFiMulti wifiMulti;
char apName[] = "ESP32 AUTO";
const char compileDate[] = __DATE__ " " __TIME__;

bool hasCredentials = false;
volatile bool isConnected = false;
bool connStatusChanged = false;

// List of Service and Characteristic UUIDs
#define SERVICE_UUID  "0000aaaa-ead2-11e7-80c1-9a214cf093ae"
#define WIFI_UUID     "00005555-ead2-11e7-80c1-9a214cf093ae"
#define CONTROL_UUID  "6b464d27-c2da-4df7-8eb9-81357ad247d5"
#define NOTIFY_UUID   "1404fd3e-979e-43de-bdb2-6234c9ec8cbe"

/** SSIDs of local WiFi networks */
String ssidPrim;
String ssidSec;
/** Password for local WiFi network */
String pwPrim;
String pwSec;

/** Characteristic for digital output */
BLECharacteristic *pCharacteristicWiFi;
BLECharacteristic *pCharacteristic3;
/** BLE Advertiser */
BLEAdvertising* pAdvertising;
/** BLE Service */
BLEService *pService;
/** BLE Server */
BLEServer *pServer;

/** Buffer for JSON string */
// MAx size is 51 bytes for frame: 
// {"ssidPrim":"","pwPrim":"","ssidSec":"","pwSec":""}
// + 4 x 32 bytes for 2 SSID's and 2 passwords
StaticJsonBuffer<200> jsonBuffer;
StaticJsonBuffer<200> JSONBuffer;
/**
 * MyServerCallbacks
 * Callbacks for client connection and disconnection
 */



class MyServerCallbacks: public BLEServerCallbacks {
  // TODO this doesn't take into account several clients being connected
  void onConnect(BLEServer* pServer) {
    Serial.println("BLE client connected");
    digitalWrite(LEDBLEMode,HIGH);
  };

  void onDisconnect(BLEServer* pServer) {
    Serial.println("BLE client disconnected");
    pAdvertising->start();
    digitalWrite(LEDBLEMode,LOW);
  }
};

/**
 * MyCallbackHandler
 * Callbacks for BLE client read/write requests
 */
class MyCallbackHandler: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    if (value.length() == 0) {
      return;
    }
    Serial.println("Received over BLE: " + String((char *)&value[0]));

    // Decode data
    int keyIndex = 0;
    for (int index = 0; index < value.length(); index ++) {
      value[index] = (char) value[index] ^ (char) apName[keyIndex];
      keyIndex++;
      if (keyIndex >= strlen(apName)) keyIndex = 0;
    }

    /** Json object for incoming data */
    JsonObject& jsonIn = jsonBuffer.parseObject((char *)&value[0]);
    if (jsonIn.success()) {
      if (jsonIn.containsKey("ssidPrim") &&
          jsonIn.containsKey("pwPrim") && 
          jsonIn.containsKey("ssidSec") &&
          jsonIn.containsKey("pwSec")) {
        ssidPrim = jsonIn["ssidPrim"].as<String>();
        pwPrim = jsonIn["pwPrim"].as<String>();
        ssidSec = jsonIn["ssidSec"].as<String>();
        pwSec = jsonIn["pwSec"].as<String>();

        Preferences preferences;
        preferences.begin("WiFiCred", false);
        preferences.putString("ssidPrim", ssidPrim);
        preferences.putString("ssidSec", ssidSec);
        preferences.putString("pwPrim", pwPrim);
        preferences.putString("pwSec", pwSec);
        preferences.putBool("valid", true);
        preferences.end();

        Serial.println("Received over bluetooth:");
        Serial.println("primary SSID: "+ssidPrim+" password: "+pwPrim);
        Serial.println("secondary SSID: "+ssidSec+" password: "+pwSec);
        hasCredentials = true;
      } else if (jsonIn.containsKey("erase")) {
        Serial.println("Received erase command");
        Preferences preferences;
        preferences.begin("WiFiCred", false);
        preferences.clear();
        preferences.end();
        hasCredentials = false;
        ssidPrim = "";
        pwPrim = "";
        ssidSec = "";
        pwSec = "";

        int err;
        err=nvs_flash_init();
        Serial.println("nvs_flash_init: " + err);
        err=nvs_flash_erase();
        Serial.println("nvs_flash_erase: " + err);
      } else if (jsonIn.containsKey("reset")) {
        WiFi.disconnect();
        esp_restart();
      }
    } else {
      Serial.println("Received invalid JSON");
    }
    jsonBuffer.clear();
  };

  void onRead(BLECharacteristic *pCharacteristic) {
    Serial.println("BLE onRead request");
    String wifiCredentials;

    /** Json object for outgoing data */
    JsonObject& jsonOut = jsonBuffer.createObject();
    jsonOut["ssidPrim"] = ssidPrim;
    jsonOut["pwPrim"] = pwPrim;
    jsonOut["ssidSec"] = ssidSec;
    jsonOut["pwSec"] = pwSec;

    // add APs
    wifiMulti.addAP(ssidPrim.c_str(), pwPrim.c_str());
    wifiMulti.addAP(ssidSec.c_str(), pwSec.c_str());

    jsonOut.printTo(wifiCredentials);

    // encode the data
    int keyIndex = 0;
    Serial.println("Stored settings: " + wifiCredentials);
    for (int index = 0; index < wifiCredentials.length(); index ++) {
      wifiCredentials[index] = (char) wifiCredentials[index] ^ (char) apName[keyIndex];
      keyIndex++;
      if (keyIndex >= strlen(apName)) keyIndex = 0;
    }
    pCharacteristicWiFi->setValue((uint8_t*)&wifiCredentials[0],wifiCredentials.length());
    jsonBuffer.clear();
  }
};
class MyCallbacks: public BLECharacteristicCallbacks 
{
    void onWrite(BLECharacteristic *pCharacteristic) 
    {
      std::string value = pCharacteristic->getValue();

 StaticJsonBuffer<200> JSONBuffer;
   std::string BLEValue = pCharacteristic->getValue();
   char jsonBLE[100];
      if (BLEValue.length() > 0) {
        Serial.println("*********");
        Serial.print("New value: ");
        for (int j = 0; j < BLEValue.length(); j++)
          jsonBLE[j]=BLEValue[j];
          Serial.println();
          Serial.println("*********");
          }
   
 JsonObject& root = JSONBuffer.parseObject(jsonBLE);  
if (!root.success()) {   //Check for errors in parsing 
    Serial.println("Parsing failed");
    delay(1000);
    return;
  }
if (root.containsKey("Switch")) {
    if (strcmp(root["Switch"], "ON1") == 0 ) {
      digitalWrite(LED1,HIGH);
      digitalWrite(LED2,LOW);
      strcpy(previous_state , (const char*)root["Switch"]);
    }
    else if (strcmp(root["Switch"], "ON2") == 0) {
     digitalWrite(LED2,HIGH);
     digitalWrite(LED1,LOW);
     strcpy(previous_state , (const char*)root["Switch"]);
    }
     if (strcmp(root["Switch"], "OFF") == 0) {
      digitalWrite(LED1,LOW);
      digitalWrite(LED2,LOW);
      strcpy(previous_state , (const char*)root["Switch"]);
    }
    }
    }
    void onRead(BLECharacteristic *pCharacteristic2) {
      String Various_Status;

    /** Json object for outgoing data */
    JsonObject& jsonStatus = JSONBuffer.createObject();
   // jsonStatus["Wifi_Status"] = wifi_status;
    jsonStatus["Switch"] = previous_state;
    // Convert JSON object into a string
    jsonStatus.printTo(Various_Status);
    pCharacteristic2->setValue((uint8_t*)&Various_Status[0],Various_Status.length());
    pCharacteristic2->setValue(Nill);
    pCharacteristic2-> notify (); 
    }
};

void initBLE() {
  // Initialize BLE and set output power
  BLEDevice::init(apName);
  BLEDevice::setPower(ESP_PWR_LVL_P7);
  uint16_t mtu = 128;
  BLEDevice::setMTU(128);
  // Create BLE Server
  pServer = BLEDevice::createServer();

  // Set server callbacks
  pServer->setCallbacks(new MyServerCallbacks());

  // Create BLE Service
  pService = pServer->createService(BLEUUID(SERVICE_UUID),20);

  // Create BLE Characteristic for WiFi settings
  pCharacteristicWiFi = pService->createCharacteristic(
    BLEUUID(WIFI_UUID),
    // WIFI_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE
  );
  pCharacteristicWiFi->setCallbacks(new MyCallbackHandler());
   // Create BLE Characteristic for BLE Operation
  BLECharacteristic *pCharacteristic2 = pService->createCharacteristic(
                                       CONTROL_UUID,
                                       BLECharacteristic::PROPERTY_READ |
                                       BLECharacteristic::PROPERTY_WRITE
                                     );
   pCharacteristic2->setCallbacks(new MyCallbacks());
// Create BLE Characteristic for Notifications 
  pCharacteristic3 = pService->createCharacteristic(
                      NOTIFY_UUID,
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
 pCharacteristic3->addDescriptor(new BLE2902());
  // Start the service
  pService->start();

  // Start advertising
  pAdvertising = pServer->getAdvertising();
  pAdvertising->start();
}

void setup() {
  pinMode(LEDBuiltInConnected,OUTPUT);
  pinMode(LEDBLEMode,OUTPUT);
  pinMode(LEDWifiNotConnected,OUTPUT);
  pinMode(LED1,OUTPUT);
  pinMode(LED2,OUTPUT);
  pinMode(Button1,INPUT_PULLUP);
  pinMode(Button2,INPUT_PULLUP);
  Serial.begin(115200);
  Serial.print("Build: ");
  Serial.println(compileDate);

  Preferences preferences;
  preferences.begin("WiFiCred", false);
  bool hasPref = preferences.getBool("valid", false);
  if (hasPref) {
    ssidPrim = preferences.getString("ssidPrim","");
    ssidSec = preferences.getString("ssidSec","");
    pwPrim = preferences.getString("pwPrim","");
    pwSec = preferences.getString("pwSec","");

    if (ssidPrim.equals("") 
        || pwPrim.equals("")
        || ssidSec.equals("")
        || pwPrim.equals("")) {
      Serial.println("Found preferences but credentials are invalid");
    } else {
      Serial.println("Read from preferences:");
      Serial.println("primary SSID: "+ssidPrim+" password: "+pwPrim);
      Serial.println("secondary SSID: "+ssidSec+" password: "+pwSec);
      hasCredentials = true;
    }
  } else {
    Serial.println("Could not find preferences, need send data over BLE");
  }
  preferences.end();

  initBLE();

  Serial.println("");
  if (hasCredentials) {
      wifiMulti.addAP(ssidPrim.c_str(), pwPrim.c_str());
    wifiMulti.addAP(ssidSec.c_str(), pwSec.c_str());
  }
  
  if(wifiMulti.run() == WL_CONNECTED) {

    Serial.println("");
    Serial.println("WiFi connected1!");
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
   Serial.println("WiFi not 1connected");
  }
}

void loop()
{  
    DHT.read11(dht_dpin);
  if(wifiMulti.run() != WL_CONNECTED) {
    digitalWrite(LEDWifiNotConnected,HIGH);
    digitalWrite(LEDBuiltInConnected,LOW);
    strcpy(wifi_status, "NotConnected");
    Serial.println("WiFi not connected!");
  } else {
    digitalWrite(LEDBuiltInConnected,HIGH);
    digitalWrite(LEDWifiNotConnected,LOW);
    strcpy(wifi_status, "Connected");
    Serial.println("WiFi connected!");
    Serial.print("SSID: ");
    Serial.println(WiFi.SSID());
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  }
  delay(1000);

   CurtainOn();
  CurtainOff();
  Notification();
}

String PassJson() { 
  StaticJsonBuffer<200> JSONbuffer1;
  JsonObject& JSONencoder1 = JSONbuffer1.createObject();
  JSONencoder1["Wifi"]= wifi_status;
  JSONencoder1["Switch_Status"]= previous_state;
  JSONencoder1["Temperature"]= String(DHT.temperature);
  JSONencoder1["Humidity"]=  String(DHT.humidity);
  JSONencoder1["Battery"]= "8.0";
  char JSONmessageBuffer1[200];
  JSONencoder1.printTo(JSONmessageBuffer1, sizeof(JSONmessageBuffer1));
  strcpy(Nill, JSONmessageBuffer1);
  return Nill;
}

void CurtainOn()
{
  int button1State, button2State;
  button1State = digitalRead(Button1);
  button2State = digitalRead(Button2);
  if ((button1State == LOW) && !(button2State == LOW)){ // if we're pushing button 1 OR button 2
  digitalWrite(LED1,HIGH);
  digitalWrite(LED2,LOW);
  strcpy(previous_state , "ON1");
  }
}
void CurtainOff()
{
  int button1State, button2State;
  button1State = digitalRead(Button1);
  button2State = digitalRead(Button2);
 if ((button2State == LOW) && !(button1State == LOW)){  // if we're pushing button 1 OR button 2
  digitalWrite(LED2,HIGH);
  digitalWrite(LED1,LOW);
  strcpy(previous_state , "ON2");
  }
}
void Notification()
{
String Gjson(PassJson());
 byte c1[20], c2[20], c3[20], c4[20], c5[20], c6[20];
String chunk1=Gjson.substring(0,20);
String chunk2=Gjson.substring(21,40);
String chunk3=Gjson.substring(41,60);
String chunk4=Gjson.substring(61,80);
String chunk5=Gjson.substring(81,100);
String chunk6=Gjson.substring(101,120);
chunk1.getBytes(c1,20);
chunk2.getBytes(c2,20);
chunk3.getBytes(c3,20);
chunk4.getBytes(c4,20);
chunk5.getBytes(c5,20);
chunk5.getBytes(c6,20);
pCharacteristic3->setValue((char*) c1);
pCharacteristic3-> notify ();
pCharacteristic3->setValue((char*) c2);
pCharacteristic3-> notify ();
pCharacteristic3->setValue((char*) c3);
pCharacteristic3-> notify ();
pCharacteristic3->setValue((char*) c4);
pCharacteristic3-> notify ();
pCharacteristic3->setValue((char*) c5);
pCharacteristic3-> notify ();
pCharacteristic3->setValue((char*) c6);
pCharacteristic3-> notify ();
}
