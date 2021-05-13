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
#include <Update.h>
#include "FS.h"
#include "SPIFFS.h"
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

#define FORMAT_SPIFFS_IF_FAILED true
#define NORMAL_MODE   0   // normal
#define UPDATE_MODE   1   // receiving firmware
#define OTA_MODE      2   // installing firmware
uint8_t updater[16384];
uint8_t updater2[16384];
#define FLASH SPIFFS
static bool writeFile = false;
static int writeLen = 0;
static int writeLen2 = 0;
static bool current = true;
static int parts = 0;
static int next = 0;
static int cur = 0;
static int MTU = 0;
static int MODE = NORMAL_MODE;

bool hasCredentials = false;
volatile bool isConnected = false;
bool connStatusChanged = false;

// List of Service and Characteristic UUIDs
#define SERVICE_UUID  "0000aaaa-ead2-11e7-80c1-9a214cf093ae"
#define WIFI_UUID     "00005555-ead2-11e7-80c1-9a214cf093ae"
#define CONTROL_UUID  "6b464d27-c2da-4df7-8eb9-81357ad247d5"
#define NOTIFY_UUID   "1404fd3e-979e-43de-bdb2-6234c9ec8cbe"
#define OTA_UUID                  "fb1e4001-54ae-4a28-9f74-dfccb248601d"
#define CHARACTERISTIC_UUID_RX    "fb1e4002-54ae-4a28-9f74-dfccb248601d"
#define CHARACTERISTIC_UUID_TX    "fb1e4003-54ae-4a28-9f74-dfccb248601d"
static BLECharacteristic* pCharacteristicTX;
static BLECharacteristic* pCharacteristicRX;

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

static void rebootEspWithReason(String reason) {
  Serial.println(reason);
  delay(1000);
  ESP.restart();
}


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

class OTA_Callbacks: public BLECharacteristicCallbacks {

    //    void onStatus(BLECharacteristic* pCharacteristic, Status s, uint32_t code) {
    //      Serial.print("Status ");
    //      Serial.print(s);
    //      Serial.print(" on characteristic ");
    //      Serial.print(pCharacteristic->getUUID().toString().c_str());
    //      Serial.print(" with code ");
    //      Serial.println(code);
    //    }

    void onNotify(BLECharacteristic *pCharacteristic) {
      uint8_t* pData;
      std::string value = pCharacteristic->getValue();
      int len = value.length();
      pData = pCharacteristic->getData();
      if (pData != NULL) {
        //        Serial.print("Notify callback for characteristic ");
        //        Serial.print(pCharacteristic->getUUID().toString().c_str());
        //        Serial.print(" of data length ");
        //        Serial.println(len);
        Serial.print("TX  ");
        for (int i = 0; i < len; i++) {
          Serial.printf("%02X ", pData[i]);
        }
        Serial.println();
      }
    }

    void onWrite(BLECharacteristic *pCharacteristic) {
      uint8_t* pData;
      std::string value = pCharacteristic->getValue();
      int len = value.length();
      pData = pCharacteristic->getData();
      if (pData != NULL) {
        //        Serial.print("Write callback for characteristic ");
        //        Serial.print(pCharacteristic->getUUID().toString().c_str());
        //        Serial.print(" of data length ");
        //        Serial.println(len);
        //        Serial.print("RX  ");
        //        for (int i = 0; i < len; i++) {         // leave this commented
        //          Serial.printf("%02X ", pData[i]);
        //        }
        //        Serial.println();

        if (pData[0] == 0xFB) {
          int pos = pData[1];
          for (int x = 0; x < len - 2; x++) {
            if (current) {
              updater[(pos * MTU) + x] = pData[x + 2];
            } else {
              updater2[(pos * MTU) + x] = pData[x + 2];
            }
          }

        } else if  (pData[0] == 0xFC) {
          if (current) {
            writeLen = (pData[1] * 256) + pData[2];
          } else {
            writeLen2 = (pData[1] * 256) + pData[2];
          }
          current = !current;
          cur = (pData[3] * 256) + pData[4];

          writeFile = true;
        } else if (pData[0] == 0xFD) {
          if (FLASH.exists("/update.bin")) {
            FLASH.remove("/update.bin");
          }
        } else if  (pData[0] == 0xFE) {
          //rebootEspWithReason("Rebooting to start OTA update");

        } else if  (pData[0] == 0xFF) {
          parts = (pData[1] * 256) + pData[2];
          MTU = (pData[3] * 256) + pData[4];
          MODE = UPDATE_MODE;

        }


      }

    }


};

void writeBinary(fs::FS &fs, const char * path, uint8_t *dat, int len) {

  //Serial.printf("Write binary file %s\r\n", path);

  File file = fs.open(path, FILE_APPEND);

  if (!file) {
    Serial.println("- failed to open file for writing");
    return;
  }
  file.write(dat, len);
  file.close();
}

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

  BLEService *otaService = pServer->createService(OTA_UUID);
  pCharacteristicTX = pService->createCharacteristic(CHARACTERISTIC_UUID_TX, BLECharacteristic::PROPERTY_NOTIFY );
  pCharacteristicRX = pService->createCharacteristic(CHARACTERISTIC_UUID_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  pCharacteristicRX->setCallbacks(new OTA_Callbacks());
  pCharacteristicTX->setCallbacks(new OTA_Callbacks());
  pCharacteristicTX->addDescriptor(new BLE2902());
  pCharacteristicTX->setNotifyProperty(true);
  otaService->start();

  // Start advertising
  pAdvertising = pServer->getAdvertising();
  pAdvertising->addServiceUUID(OTA_UUID);
  pAdvertising->setScanResponse(true);
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

  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }

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

  switch (MODE) {

    case NORMAL_MODE:
    
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

   break;

    case UPDATE_MODE:

      if (cur + 1 == parts) { // received complete file
        uint8_t com[] = {0xF2, (cur + 1) / 256, (cur + 1) % 256};
        pCharacteristicTX->setValue(com, 3);
        pCharacteristicTX->notify();
        delay(50);
        MODE = OTA_MODE;
      }

      if (writeFile) {
        if (!current) {
          writeBinary(FLASH, "/update.bin", updater, writeLen);
        } else {
          writeBinary(FLASH, "/update.bin", updater2, writeLen2);
        }
        writeFile = false;
      }

      break;

    case OTA_MODE:
      updateFromFS(FLASH);
      break;

  }

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

void sendOtaResult(String result) {
  pCharacteristicTX->setValue(result.c_str());
  pCharacteristicTX->notify();
  delay(200);
}


void performUpdate(Stream &updateSource, size_t updateSize) {
  char s1 = 0x0F;
  String result = String(s1);
  if (Update.begin(updateSize)) {
    size_t written = Update.writeStream(updateSource);
    if (written == updateSize) {
      Serial.println("Written : " + String(written) + " successfully");
    }
    else {
      Serial.println("Written only : " + String(written) + "/" + String(updateSize) + ". Retry?");
    }
    result += "Written : " + String(written) + "/" + String(updateSize) + " [" + String((written / updateSize) * 100) + "%] \n";
    if (Update.end()) {
      Serial.println("OTA done!");
      result += "OTA Done: ";
      if (Update.isFinished()) {
        Serial.println("Update successfully completed. Rebooting...");
        result += "Success!\n";
      }
      else {
        Serial.println("Update not finished? Something went wrong!");
        result += "Failed!\n";
      }

    }
    else {
      Serial.println("Error Occurred. Error #: " + String(Update.getError()));
      result += "Error #: " + String(Update.getError());
    }
  }
  else
  {
    Serial.println("Not enough space to begin OTA");
    result += "Not enough space for OTA";
  }
  if (deviceConnected) {
    sendOtaResult(result);
    delay(5000);
  }
}

void updateFromFS(fs::FS &fs) {
  File updateBin = fs.open("/update.bin");
  if (updateBin) {
    if (updateBin.isDirectory()) {
      Serial.println("Error, update.bin is not a file");
      updateBin.close();
      return;
    }

    size_t updateSize = updateBin.size();

    if (updateSize > 0) {
      Serial.println("Trying to start update");
      performUpdate(updateBin, updateSize);
    }
    else {
      Serial.println("Error, file is empty");
    }

    updateBin.close();

    // when finished remove the binary from spiffs to indicate end of the process
    Serial.println("Removing update file");
    fs.remove("/update.bin");

    rebootEspWithReason("Rebooting to complete OTA update");
  }
  else {
    Serial.println("Could not load update.bin from spiffs root");
  }
}
