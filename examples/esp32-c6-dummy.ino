#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#define WIFI_CHANNEL 1 
const bool DEBUG = true; 

#define ESPNOW_TYPE_POCSAG 0x11
#define ESPNOW_TYPE_PING   0x12
#define ESPNOW_TYPE_PONG   0x13

// Base header
struct __attribute__((packed)) MeshHeader {
  uint8_t  type;
  uint8_t  ttl;
  uint32_t msgId;
  uint8_t  destMac[6];
};

struct __attribute__((packed)) EspNowPongPacket {
  uint8_t  type;
  uint8_t  ttl;
  uint32_t msgId;
  uint8_t  destMac[6];
  uint8_t  nodeMac[6];
};

uint8_t broadcastMac[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
uint8_t myMac[6];

// Deduplication
#define SEEN_SIZE 30
uint32_t seen[SEEN_SIZE];
uint8_t sIdx = 0;
bool isSeen(uint32_t id) { for(int i=0; i<SEEN_SIZE; i++) if(seen[i]==id) return true; return false; }
void markSeen(uint32_t id) { seen[sIdx] = id; sIdx = (sIdx + 1) % SEEN_SIZE; }

// --- RECV CALLBACK ---
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  if (len < sizeof(MeshHeader)) return;
  MeshHeader pkt;
  memcpy(&pkt, incomingData, sizeof(MeshHeader));

  if (!isSeen(pkt.msgId)) {
    markSeen(pkt.msgId);
    bool isForMe = (memcmp(pkt.destMac, myMac, 6) == 0) || (memcmp(pkt.destMac, broadcastMac, 6) == 0);

    if (DEBUG) Serial.printf("\n[RX] Type: %02X | ID: %lu | TTL: %d\n", pkt.type, pkt.msgId, pkt.ttl);

    // 1. If it's a PING, respond with a PONG
    if (pkt.type == ESPNOW_TYPE_PING) {
      if (DEBUG) Serial.println("[PING RX] Sending PONG response...");
      EspNowPongPacket pong = {};
      pong.type = ESPNOW_TYPE_PONG;
      pong.ttl = 4; // Hops to get back to master
      pong.msgId = esp_random();
      memcpy(pong.destMac, info->src_addr, 6); // Send back to whoever transmitted this
      memcpy(pong.nodeMac, myMac, 6);          // Identify myself
      
      esp_now_send(broadcastMac, (uint8_t*)&pong, sizeof(pong));
    }

    // 2. Digipeat the packet forward if TTL allows
    if (pkt.ttl > 0 && !isForMe) {
      uint8_t relayBuffer[250];
      memcpy(relayBuffer, incomingData, len);
      MeshHeader* relayHeader = (MeshHeader*) relayBuffer;
      relayHeader->ttl--; 
      
      if (DEBUG) Serial.println("[RELAY] Digipeating packet forward...");
      esp_now_send(broadcastMac, relayBuffer, len);
    }
  }
}

void setup() {
  if (DEBUG) { Serial.begin(115200); delay(1000); }
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  WiFi.macAddress(myMac);

  if (DEBUG) Serial.printf("[SYSTEM] Relay Node MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", myMac[0], myMac[1], myMac[2], myMac[3], myMac[4], myMac[5]);

  esp_now_init();
  esp_now_register_recv_cb(onDataRecv);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastMac, 6);
  peer.channel = WIFI_CHANNEL;
  esp_now_add_peer(&peer);
}

void loop() { delay(1000); }
