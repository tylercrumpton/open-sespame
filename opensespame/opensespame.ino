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
#include <Adafruit_PN532.h>

// Connect to the PN532 using a hardware SPI connection. 
// Pins need to be connected as follows:
//  PN532 <---> NodeMCU
//  -------------------
//   MOSI <---> D7
//   MISO <---> D6
//    SCK <---> D5
//     SS <---> D4

#define PN532_SS   (D4)
Adafruit_PN532 nfc(PN532_SS);

// Define states for state machine:
enum doorStates {
  DOOR_CLOSED_AND_LOCKED,
  DOOR_CLOSED_AND_UNLOCKED,
  DOOR_OPEN_AND_UNLOCKED,
  DOOR_RECLOSED_AND_UNLOCKED,
  UNKNOWN
};

doorStates currentState = UNKNOWN;

void setup(void) {
  Serial.begin(115200);
  Serial.println("Booting open-sespame...");

  determineCurrentState();

  nfc.begin();

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (! versiondata) {
    Serial.print("Didn't find PN53x board");
    while (1); // halt
  }
  
  // Got ok data, print it out!
  Serial.print("Found chip PN5"); Serial.println((versiondata>>24) & 0xFF, HEX); 
  Serial.print("Firmware ver. "); Serial.print((versiondata>>16) & 0xFF, DEC); 
  Serial.print('.'); Serial.println((versiondata>>8) & 0xFF, DEC);
  
  // Set the max number of retry attempts to read from a card
  // This prevents us from waiting forever for a card, which is
  // the default behaviour of the PN532.
  nfc.setPassiveActivationRetries(0xFE);
  
  // configure board to read RFID tags
  nfc.SAMConfig();
  
  Serial.println("Waiting for an ISO14443A card");
}

void loop(void) {
  if (currentState == DOOR_CLOSED_AND_LOCKED) {
    String nfcID = checkNFC();
    if (isValidID(nfcID)) {
      unlockDoor();
      startUnlockTimeout();
      currentState = DOOR_CLOSED_AND_UNLOCKED;
    }
  } else if (currentState == DOOR_CLOSED_AND_UNLOCKED){
    String nfcID = checkNFC();
  }
  // Wait 1 second before continuing
  delay(1000);
}

String checkNFC() {
  boolean success;
  uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };   // Buffer to store the returned UID
  uint8_t uidLength;                         // Length of the UID (4 or 7 bytes depending on ISO14443A card type)
  String strUID = "";
  
  // Wait for an ISO14443A type cards (Mifare, etc.).  When one is found
  // 'uid' will be populated with the UID, and uidLength will indicate
  // if the uid is 4 bytes (Mifare Classic) or 7 bytes (Mifare Ultralight)
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, &uid[0], &uidLength);
  
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
  return true;
}

bool isValidID(String nfcID) {
  bool isValid = false;
  if (nfcID != "") {
    isValid = true;
  }
  return isValid;
}

void lockDoor() {

}

void unlockDoor() {

}

void startUnlockTimeout() {

}

