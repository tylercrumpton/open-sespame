/**************************************************************************/
/*  Modified from iso14443a_uid.pde from https://github.com/adafruit/Adafruit-PN532
 *    Copyright (c) 2012, Adafruit Industries
 *    All rights reserved.
 *    
 *  Modifications by Tyler Crumpton
 *    Copyright (c) 2016, Tyler Crumpton
 */ 
/**************************************************************************/
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_PN532.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <Hash.h>
#include <ESP8266WebServer.h>
#include <FS.h>

extern "C" {
#include "user_interface.h"
}

// Function definitions:
extern String checkNFC();
extern String hexlify(uint8_t byte);
extern void determineCurrentState();
extern bool isDoorClosed();
extern bool isValidID(String nfcID);
extern void lockDoor();
extern void unlockDoor();
extern void startUnlockTimeout();
extern void restartUnlockTimeout();
extern void unlockTimeoutCallback(void *pArg);
extern void startRelockTimeout();
extern void restartRelockTimeout();
extern void relockTimeoutCallback(void *pArg);
extern void doorChanged();
extern void doorOpened();
extern void doorClosed();
extern void connectToWiFi();
extern void connectToMQTT();
extern void scheduleMessage(String subTopic, String message);
extern void sendMessages();
extern bool initializeNFCReader();
extern bool isNFCReaderPresent();

// Connect to the PN532 using a hardware SPI connection. 
// Pins need to be connected as follows:
//  PN532 <---> NodeMCU
//  -------------------
//   MOSI <---> D7
//   MISO <---> D6
//    SCK <---> D5
//     SS <---> D4
#define IO_DOOR_SENSOR  (D0)
#define IO_DOOR_UNLOCK  (D1)
#define IO_PN532_SS     (D4)

Adafruit_PN532 nfc(IO_PN532_SS);

#include "config.h"

WiFiClient wclient;
PubSubClient client(wclient);

ESP8266WebServer server(80);

void handleRoot() {
  server.send(200, "text/plain", "Hi there! You found the open-sESPame device! Congrats! You should buy yourself a cookie!");
}

void returnOK() {
  server.send(200, "text/plain", "");
}

void handleFileUpload(){
  if(server.uri() != "/upload") return;
  String csvData = server.arg(0);
  Serial.print(server.argName(0));
  Serial.print(": ");
  Serial.println(csvData);

  File csvFile = SPIFFS.open("/nfc_ids.csv", "w");

  unsigned char* csvBytes = (unsigned char*) csvData.c_str();

  csvFile.write(csvBytes, csvData.length());
  csvFile.close();

  returnOK();
}

void handleNotFound(){
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i<server.args(); i++){
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

// Define states for state machine:
enum doorStates {
  DOOR_CLOSED_AND_LOCKED,
  DOOR_CLOSED_AND_UNLOCKED,
  DOOR_OPEN_AND_UNLOCKED,
  DOOR_RECLOSED_AND_UNLOCKED,
  UNKNOWN
};

doorStates currentState = UNKNOWN;

bool nfcReaderInitialized = false;

String scheduledMessages[8][2];
int numScheduledMessages = 0;

// Define a timer to use for timeouts:
os_timer_t timer;

void setup(void) {
  SPIFFS.begin();

  client.setServer(mqttHost, mqttPort);

  Serial.begin(115200);
  Serial.println("Booting open-sespame...");

  Serial.print("Max packet size: "); Serial.println(MQTT_MAX_PACKET_SIZE);

  scheduleMessage("/device", "{\"status\":\"booted\"}");

  // Enable GPIO interrupts:
  pinMode(IO_DOOR_SENSOR, INPUT);
  attachInterrupt(IO_DOOR_SENSOR, doorChanged, CHANGE);
  Serial.print("Door sensor on pin "); Serial.println(IO_DOOR_SENSOR);

  pinMode(IO_DOOR_UNLOCK, OUTPUT);
  Serial.print("Door unlock on pin "); Serial.println(IO_DOOR_UNLOCK);

  determineCurrentState();

  // Connect to WiFi:
  connectToWiFi();

  server.on("/", handleRoot);
  server.on("/upload", HTTP_POST, handleFileUpload);
  server.onNotFound(handleNotFound);

  server.begin();

  nfc.begin();
  Serial.println("Initializing NFC reader...");
  if ( initializeNFCReader() ) {
    nfcReaderInitialized = true;
    Serial.println("NFC reader initialized.");
  } else {
    nfcReaderInitialized = false;
    Serial.println("No dice. Continuing anyway. Will try to reinit later.");
  }

  Serial.println("boot done.");
  
}

void loop(void) {
  if ( isNFCReaderPresent() ) {
    if ( ! nfcReaderInitialized ) {
      //it's back! reinitialize it
      Serial.println("NFC reader is back!");
      nfcReaderInitialized = initializeNFCReader();
    } //end if-initialized
    //so now it's here and it's also initialized. check for tags
    if (currentState == DOOR_CLOSED_AND_LOCKED) {
      String nfcID = checkNFC();
      if (isValidID(nfcID)) {
        unlockDoor();
        startUnlockTimeout();
        currentState = DOOR_CLOSED_AND_UNLOCKED;
      } //end if-valid
    } else if (currentState == DOOR_CLOSED_AND_UNLOCKED) {
      String nfcID = checkNFC();
      if (isValidID(nfcID)) {
        unlockDoor();
        restartUnlockTimeout();
      } //end if-valid
    } //end if-closed-and-unlocked
  } else {
    //The reader is not present, so obviously it is not initialized.
    nfcReaderInitialized = false;
    Serial.println("Not checking for tags because NFC reader isn't here.");
    //you would think we would want a delay here.
    //but actually checking for the NFC reader when it is not present takes like, almost a second.
    //so whatever
  }

  sendMessages();
  yield();
  client.loop();
  server.handleClient();
}

String checkNFC() {
  boolean success;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };   // Buffer to store the returned UID
  uint8_t uidLength;                         // Length of the UID (4 or 7 bytes depending on ISO14443A card type)
  String strUID = "";
  
  // Wait for an ISO14443A type cards (Mifare, etc.).  When one is found
  // 'uid' will be populated with the UID, and uidLength will indicate
  // if the uid is 4 bytes (Mifare Classic) or 7 bytes (Mifare Ultralight)
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, &uid[0], &uidLength, 1000);
  
  if (success) {
    Serial.println("Found a card!");
    Serial.print("UID Length: ");Serial.print(uidLength, DEC);Serial.println(" bytes");
    Serial.print("UID Value: ");
    for (uint8_t i=0; i < uidLength; i++) 
    {
      Serial.print(" 0x");Serial.print(uid[i], HEX); 
      strUID += hexlify(uid[i]);
    }
    Serial.println("");
  }
  else
  {
    // PN532 probably timed out waiting for a card
    Serial.println("Timed out waiting for a card");
  }
  return strUID;
}

String hexlify(uint8_t byte) {
  // Conver the integer to a string with the hex value:
  String hexedString = String(byte, HEX);

  // Add prepended '0' if needed:
  if (hexedString.length() == 1) {
    hexedString = "0" + hexedString;
  }

  return hexedString;
}

void determineCurrentState() {
  if (isDoorClosed()) {
    lockDoor();
    currentState = DOOR_CLOSED_AND_LOCKED;
  } else {
    unlockDoor();
    currentState = DOOR_OPEN_AND_UNLOCKED;
  }
}

bool isDoorClosed() {
  switch (currentState) {
    case DOOR_CLOSED_AND_LOCKED:
    case DOOR_CLOSED_AND_UNLOCKED:
    case DOOR_RECLOSED_AND_UNLOCKED:
      return true;
    default:
      return false;
  }
}

bool isStringInString(String needle, String haystack) {
  for (int i = 0; i <= haystack.length() - needle.length(); i++) {
    if (haystack.substring(i, needle.length() + i) == needle) {
      return true;
    }
  }
  return false;
}

bool isValidID(String nfcID) {
  bool isValid = false;
  if (nfcID != "") {
    Serial.print("SHA:");

    String shaID = sha1(nfcID);

    Serial.println(shaID);

    File file = SPIFFS.open("/nfc_ids.csv", "r");

    if (!file) {
        Serial.println("File open failed.");
        scheduleMessage("/error", "{\"error\":\"NFC_FILE_OPEN_ERROR\",\"message\":\"NFC CSV file open failed.\"}");
        return false;
    }
    //for (int i=1; i<=19; i++){
    while (file.available()) {
      String s=file.readStringUntil('\n');
      if (isStringInString(shaID, s)) {
        isValid = true;
        String user = s.substring(0, s.indexOf(','));
        scheduleMessage("/user", "{\"valid\":true,\"user\":\"" + user + "\",\"method\":\"nfc\",\"hashedId\":\"" + shaID + "\"}");
      }
    }

    if (!isValid) {
      scheduleMessage("/error", "{\"error\":\"NO_USER_FOR_NFC\",\"message\":\"No user found for NFC ID.\",\"hashedId\":\"" + shaID + "\"}");
    }
    file.close();
  } 
  return isValid;
}

void lockDoor() {
  Serial.println("Locking door.");
  scheduleMessage("/lock", "{\"status\":\"locked\"}");
}

void unlockDoor() {
  Serial.println("Unlocking door.");
  scheduleMessage("/lock", "{\"status\":\"unlocked\"}");
  digitalWrite(IO_DOOR_UNLOCK, HIGH);
  delay(100);
  digitalWrite(IO_DOOR_UNLOCK, LOW);
}

void startUnlockTimeout() {
  os_timer_setfn(&timer, unlockTimeoutCallback, NULL);
  os_timer_arm(&timer, 10000, false);
  Serial.println("Door unlock timer started.");
}

void restartUnlockTimeout() {
  os_timer_disarm(&timer);
  os_timer_arm(&timer, 10000, false);
  Serial.println("Door unlock timer restarted.");
}

void unlockTimeoutCallback(void *pArg) {
  Serial.println("Door unlock timeout elapsed.");
  lockDoor();
  currentState = DOOR_CLOSED_AND_LOCKED;
}

void startRelockTimeout() {
  os_timer_setfn(&timer, relockTimeoutCallback, NULL);
  os_timer_arm(&timer, 10000, false);
  Serial.println("Door relock timer started.");
}

void restartRelockTimeout() {
  os_timer_disarm(&timer);
  os_timer_arm(&timer, 10000, false);
  Serial.println("Door relock timer restarted.");
}

void relockTimeoutCallback(void *pArg) {
  Serial.println("Door relock timeout elapsed.");
  lockDoor();
  currentState = DOOR_CLOSED_AND_LOCKED;
}

void doorChanged() {
  bool status = digitalRead(IO_DOOR_SENSOR);
  if (status) {
    doorClosed();
  } else {
    doorOpened();
  }
}

void doorOpened() {
  Serial.println("Door opened");
  if (currentState == DOOR_CLOSED_AND_UNLOCKED) {
    os_timer_disarm(&timer);
    currentState = DOOR_OPEN_AND_UNLOCKED;
  } else if (currentState == DOOR_RECLOSED_AND_UNLOCKED) {
    restartRelockTimeout();
  }
  scheduleMessage("/doorsensor", "{\"status\":\"open\"}");
}

void doorClosed() {
  Serial.println("Door closed");
  if (currentState == DOOR_OPEN_AND_UNLOCKED) {
    currentState = DOOR_RECLOSED_AND_UNLOCKED;
    startRelockTimeout();
  }
  scheduleMessage("/doorsensor", "{\"status\":\"closed\"}");
}

void connectToWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("Connecting to ");
    Serial.print(ssid);
    Serial.println("...");
    WiFi.begin(ssid, pass);

    if (WiFi.waitForConnectResult() != WL_CONNECTED) {
      Serial.println("Connection not successful.");
      return;
    }
    
    Serial.print("WiFi connected; IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi already connected!");
  }
}

void connectToMQTT() {
  if (!client.connected()) {
      if (client.connect("doorClient")) {
        Serial.println("MQTT connected.");
      } else {
        Serial.println("MQTT failed to connect.");
      }
  }
}

void scheduleMessage(String subTopic, String message) {
  if (numScheduledMessages <= 8) {
    ++numScheduledMessages;
    scheduledMessages[numScheduledMessages-1][0] = message;
    scheduledMessages[numScheduledMessages-1][1] = subTopic;
  }
}

void sendMessages() {
  for (int i=1; i<=numScheduledMessages; ++i) {
    connectToMQTT();
    String message = mqttTopic+scheduledMessages[i-1][1];
    String topic = scheduledMessages[i-1][0].c_str();
    client.publish(message.c_str(), topic.c_str());
  }
  numScheduledMessages = 0;
}

bool initializeNFCReader() {
  Serial.println("Starting NFC...");
  if ( ! isNFCReaderPresent() ) {
    Serial.println("Didn't find PN53x board");
    Serial.println("NFC init failed!");
  } else {
    // Set the max number of retry attempts to read from a card
    // This prevents us from waiting forever for a card, which is
    // the default behaviour of the PN532.
    nfc.setPassiveActivationRetries(0xFE);
    
    // configure board to read RFID tags
    nfc.SAMConfig();
    
    Serial.println("NFC init done.");
    return true;
  }
  return false;
}

bool isNFCReaderPresent() {
  uint32_t versiondata = nfc.getFirmwareVersion();
  if ( versiondata ) {
    //Serial.print("Found chip PN5"); Serial.println((versiondata>>24) & 0xFF, HEX); 
    //Serial.print("Firmware ver. "); Serial.print((versiondata>>16) & 0xFF, DEC); 
    //Serial.print('.'); Serial.println((versiondata>>8) & 0xFF, DEC);
    return true;
  }
  return false;
}

