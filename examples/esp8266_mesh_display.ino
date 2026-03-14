#include <ESP8266WiFi.h>
#include <espnow.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// --- CONFIGURATION ---
// CRITICAL: Must match the channel of the  router so it hears the Master Node
#define WIFI_CHANNEL 1 

// Set to false to disable all Serial logging for silent production deployment
const bool DEBUG = true; 

// --- HARDWARE ---
#define OLED_SDA 14
#define OLED_SCL 12
Adafruit_SSD1306 display(128, 64, &Wire, -1);

// --- MESH STRUCT ---
#define ESPNOW_TYPE_POCSAG   0x11
#define POCSAG_MSG_MAX_LEN   80

struct __attribute__((packed)) EspNowPocsagPacket {
  uint8_t  type;
  uint8_t  ttl;
  uint32_t msgId;
  uint8_t  destMac[6];
  uint32_t ric;
  uint8_t  functional;
  char     message[POCSAG_MSG_MAX_LEN + 1];
};

uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t myMac[6];

// --- DEDUPLICATION ENGINE ---
#define SEEN_HISTORY_SIZE 20
uint32_t seenMessages[SEEN_HISTORY_SIZE];
uint8_t seenIndex = 0;

bool isMessageSeen(uint32_t id) {
  for (int i = 0; i < SEEN_HISTORY_SIZE; i++) {
    if (seenMessages[i] == id) return true;
  }
  return false;
}

void markMessageSeen(uint32_t id) {
  seenMessages[seenIndex] = id;
  seenIndex = (seenIndex + 1) % SEEN_HISTORY_SIZE;
}

// --- WDT SAFE BUFFER ---
volatile bool newDataReady = false;
EspNowPocsagPacket rxPacket;
uint8_t senderMac[6];

// --- CALLBACK ---
// Keep this lightning-fast to prevent ESP8266 Watchdog resets
void onDataRecv(uint8_t * mac, uint8_t *incomingData, uint8_t len) {
  if (len < sizeof(EspNowPocsagPacket)) return;
  
  if (incomingData[0] == ESPNOW_TYPE_POCSAG) {
    memcpy(&rxPacket, incomingData, sizeof(rxPacket));
    memcpy(senderMac, mac, 6);
    newDataReady = true;
  }
}

void setup() {
  if (DEBUG) {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n=== DIY MESH NODE (ESP8266) ===");
  }

  Wire.begin(OLED_SDA, OLED_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(1);

  // Set to Station mode and hardcode the channel
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  wifi_set_channel(WIFI_CHANNEL); // ESP8266 specific channel command
  WiFi.macAddress(myMac);

  if (DEBUG) {
    Serial.printf("[WIFI] Locked to Channel: %d (Matching TechInc router)\n", WIFI_CHANNEL);
    Serial.printf("[SYSTEM] My MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", 
                  myMac[0], myMac[1], myMac[2], myMac[3], myMac[4], myMac[5]);
  }

  // Boot Display
  display.setCursor(0,0);
  display.println("MESH NODE ONLINE");
  display.printf("MAC: %02X:%02X:%02X\n", myMac[3], myMac[4], myMac[5]); 
  display.display();
  delay(2000);

  if (esp_now_init() != 0) {
    if (DEBUG) Serial.println("[ESP-NOW] Init Failed");
    return;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
  esp_now_register_recv_cb(onDataRecv);
  esp_now_add_peer(broadcastMac, ESP_NOW_ROLE_COMBO, WIFI_CHANNEL, NULL, 0);

  if (DEBUG) Serial.println("[SYSTEM] Digipeater active and listening...\n");
}

void loop() {
  // Process the packet safely outside the interrupt
  if (newDataReady) {
    rxPacket.message[POCSAG_MSG_MAX_LEN] = '\0'; 

    if (!isMessageSeen(rxPacket.msgId)) {
      markMessageSeen(rxPacket.msgId);

      if (DEBUG) {
        Serial.println("\n-----------------------------------------");
        Serial.printf("[RX] New Packet ID: %lu from %02X:%02X:%02X:%02X:%02X:%02X\n", 
                      rxPacket.msgId, 
                      senderMac[0], senderMac[1], senderMac[2], 
                      senderMac[3], senderMac[4], senderMac[5]);
        Serial.printf("     RIC: %lu | MSG: %s\n", rxPacket.ric, rxPacket.message);
      }

      bool isForMe = (memcmp(rxPacket.destMac, myMac, 6) == 0) || (memcmp(rxPacket.destMac, broadcastMac, 6) == 0);

      // ACTION 1: The packet reached its final destination
      if (isForMe) {
        if (DEBUG) Serial.println("[DESTINATION REACHED] Processing payload.");
        display.clearDisplay();
        display.setCursor(0,0);
        display.println(">> POCSAG PAGE");
        display.printf("RIC: %lu\n", rxPacket.ric);
        display.setCursor(0, 30);
        display.println(rxPacket.message);
        display.display();
      } 
      
      // ACTION 2: The packet is for someone else, relay it!
      else if (rxPacket.ttl > 0) {
        rxPacket.ttl--; 
        
        if (DEBUG) Serial.printf("[RELAY] Digipeating... TTL is now %d\n", rxPacket.ttl);
        
        esp_now_send(broadcastMac, (uint8_t *) &rxPacket, sizeof(rxPacket));

        // Briefly show the relay action on screen
        display.clearDisplay();
        display.setCursor(0,0);
        display.println(">> MESH RELAY");
        display.printf("ID: %lu\n", rxPacket.msgId);
        display.printf("TTL left: %d\n", rxPacket.ttl);
        display.display();
      } 
      
      // ACTION 3: Packet died in transit
      else {
        if (DEBUG) Serial.println("[DROP] Packet TTL expired. Not relaying.");
      }
      
      if (DEBUG) Serial.println("-----------------------------------------");
    }
    
    // Clear flag for the next interrupt
    newDataReady = false;
  }
}
