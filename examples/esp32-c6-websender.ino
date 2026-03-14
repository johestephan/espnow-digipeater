#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WebServer.h>

// --- CONFIGURATION ---
const char* ssid = "YOUR NETWORK";
const char* password = "YOUR PASSWORD";

// Toggle verbose serial logging
const bool DEBUG = true; 

WebServer server(80);

// --- MESH-ENABLED STRUCT ---
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

// --- HTML INTERFACE ---
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>DIY Mesh Injector</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: monospace; margin: 40px; background-color: #1e1e1e; color: #d4d4d4; }
    .container { max-width: 500px; margin: 0 auto; background: #252526; padding: 20px; border-radius: 8px; border: 1px solid #3c3c3c; box-shadow: 0 4px 10px rgba(0,0,0,0.5); }
    h2 { text-align: center; color: #4CAF50; border-bottom: 1px solid #3c3c3c; padding-bottom: 10px; }
    label { font-weight: bold; display: block; margin-top: 15px; color: #9cdcfe; }
    input[type="text"], input[type="number"] { width: 100%; padding: 10px; margin-top: 5px; border: 1px solid #555; background-color: #3c3c3c; color: white; border-radius: 4px; box-sizing: border-box; font-family: monospace; }
    input[type="submit"] { width: 100%; padding: 12px; margin-top: 25px; background-color: #4CAF50; color: white; border: none; border-radius: 4px; font-size: 16px; font-weight: bold; cursor: pointer; }
    input[type="submit"]:hover { background-color: #45a049; }
  </style>
</head>
<body>
  <div class="container">
    <h2>[ DIY MESH TRANSMITTER ]</h2>
    <form action="/send" method="POST">
      <label for="mac">Final Destination MAC:</label>
      <input type="text" id="mac" name="mac" placeholder="e.g. BC:FF:4D:80:2E:8E" required>
      
      <label for="ttl">Time-To-Live (Hops):</label>
      <input type="number" id="ttl" name="ttl" value="3" min="1" max="10" required>
      
      <label for="ric">POCSAG RIC:</label>
      <input type="number" id="ric" name="ric" value="1234567" required>

      <label for="message">Payload Message:</label>
      <input type="text" id="message" name="message" placeholder="Enter test payload..." maxlength="80" required>
      
      <input type="submit" value="INJECT INTO MESH">
    </form>
  </div>
</body>
</html>
)rawliteral";

// --- HELPERS ---
bool parseMacStr(String macStr, uint8_t* macArr) {
  int values[6];
  if (sscanf(macStr.c_str(), "%x:%x:%x:%x:%x:%x", &values[0], &values[1], &values[2], &values[3], &values[4], &values[5]) == 6) {
    for (int i = 0; i < 6; i++) macArr[i] = (uint8_t)values[i];
    return true;
  }
  return false;
}

// --- CORE 3.x CALLBACK ---
void onDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  if (DEBUG) {
    Serial.printf("[ESP-NOW TX] Hardware Delivery Status: %s\n", 
                  (status == ESP_NOW_SEND_SUCCESS ? "SUCCESS" : "FAIL"));
  }
}

// --- WEB ROUTES ---
void handleRoot() {
  server.send(200, "text/html", htmlPage);
  if (DEBUG) Serial.println("[WEB] Served Root UI.");
}

void handleSend() {
  if (server.hasArg("mac") && server.hasArg("message")) {
    uint8_t targetMac[6];
    
    if (parseMacStr(server.arg("mac"), targetMac)) {
      EspNowPocsagPacket pkt = {};
      
      // 1. Construct the Routing Headers
      pkt.type = ESPNOW_TYPE_POCSAG;
      pkt.ttl = server.arg("ttl").toInt();
      pkt.msgId = esp_random(); // Unique ID to prevent broadcast storms
      memcpy(pkt.destMac, targetMac, 6);
      
      // 2. Construct the Payload
      pkt.ric = server.arg("ric").toInt();
      pkt.functional = 3; 
      strncpy(pkt.message, server.arg("message").c_str(), POCSAG_MSG_MAX_LEN);
      pkt.message[POCSAG_MSG_MAX_LEN] = '\0';

      // 3. Inject into the Mesh via Broadcast
      esp_err_t result = esp_now_send(broadcastMac, (uint8_t *) &pkt, sizeof(pkt));
      
      if (result == ESP_OK) {
        if (DEBUG) {
          Serial.println("\n-----------------------------------------");
          Serial.printf("[MESH INJECT] MSG ID: %lu\n", pkt.msgId);
          Serial.printf("   -> Target: %s | TTL: %d\n", server.arg("mac").c_str(), pkt.ttl);
          Serial.printf("   -> Payload: %s\n", pkt.message);
          Serial.println("-----------------------------------------");
        }
        server.sendHeader("Location", "/");
        server.send(303);
      } else {
        if (DEBUG) Serial.println("[ERROR] ESP-NOW hardware transmission failed.");
        server.send(500, "text/plain", "Error: ESP-NOW hardware transmission failed.");
      }
    } else {
      if (DEBUG) Serial.println("[ERROR] Invalid MAC Address format submitted.");
      server.send(400, "text/plain", "Error: Invalid MAC format. Use AA:BB:CC:DD:EE:FF");
    }
  }
}


void setup() {
  if (DEBUG) {
    Serial.begin(115200);
    
    // Force the C6 to wait until the Serial Monitor connects
    // Add a 5-second timeout so it doesn't hang forever if running headless
    uint32_t t = millis();
    while (!Serial && (millis() - t) < 5000) {
      delay(10);
    }
    
    Serial.println("\n=== DIY MESH MASTER NODE ===");
  }

  // Connect to the home network
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  
  if (DEBUG) Serial.print("[WIFI] Connecting to Wifi...");
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    if (DEBUG) Serial.print(".");
  }
  
  // CRITICAL: ESP-NOW must operate on the router's channel
  uint8_t primaryChan = WiFi.channel();
  
  if (DEBUG) {
    Serial.printf("\n[WIFI] Connected! Web UI IP: http://%s\n", WiFi.localIP().toString().c_str());
    Serial.printf("[WIFI] Operating Channel: %d\n", primaryChan);
  }

  // Initialize ESP-NOW
  if (esp_now_init() != ESP_OK) {
    if (DEBUG) Serial.println("[ESP-NOW] Initialization Failed!");
    return;
  }
  esp_now_register_send_cb(onDataSent);

  // Register the Universal Broadcast Peer using the router's channel
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastMac, 6);
  peerInfo.channel = primaryChan; 
  peerInfo.encrypt = false;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    if (DEBUG) Serial.println("[ESP-NOW] Failed to register broadcast peer!");
  }

  // Start Web Server
  server.on("/", HTTP_GET, handleRoot);
  server.on("/send", HTTP_POST, handleSend);
  server.begin();
  
  if (DEBUG) Serial.println("[SYSTEM] Server running and ready for injection.");
}

void loop() {
  server.handleClient();
}
