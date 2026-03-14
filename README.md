# DIY ESP-NOW Digipeater Mesh

A lightweight, stateless mesh network built on raw 802.11 ESP-NOW Action Frames. This project bypasses the heavy overhead of standard TCP/IP mesh libraries (like `painlessMesh`) in favor of a lightning-fast, APRS-style digipeater architecture.

## 📡 The Concept

Unlike traditional routing where nodes maintain complex connection tables, this network uses a **"fire-and-forget" broadcast topology**. 

When a payload is injected into the network, it is broadcast to the universal MAC address (`FF:FF:FF:FF:FF:FF`). Any node in range that hears the packet will:
1. Check the `msgId` to see if it has already processed this payload (Deduplication).
2. Check if it is the final destination. If yes, it processes the payload.
3. If not, it checks the `ttl` (Time-To-Live). If `ttl > 0`, it decrements the TTL by 1 and instantly re-broadcasts the packet for the next node to hear.

This creates a highly resilient, self-healing network without any connection handshakes or memory-heavy routing tables.

## 🏗️ Network Architecture

This repository contains three distinct node profiles:

### 1. The Master Injector (ESP32-C6)
* Connects to your standard Wi-Fi router to host a local Web UI.
* Acts as the bridge between your traditional network and the ESP-NOW mesh.
* Takes user input (Destination MAC, TTL, Payload), wraps it in the custom Mesh routing header, generates a unique `msgId`, and blasts it into the ether.

### 2. The Relay / Digipeater Node (Generic ESP32)
* A headless, silent node.
* Sits on the designated Wi-Fi channel and listens for ESP-NOW broadcasts.
* Decrements the TTL and relays packets to extend physical range.

### 3. The Display Gateway (ESP8266 HW364A)
* Features an SSD1306 OLED via I2C.
* Safely buffers incoming mesh packets outside the hardware interrupt to prevent Watchdog Timer (WDT) crashes.
* Displays the final routed payloads (POCSAG paging messages) on the screen.

## ⚙️ Installation & Setup

### Prerequisite: The Wi-Fi Channel Rule
Because ESP-NOW does not connect to an Access Point, it cannot automatically follow channel changes. 
The **Master Injector** dictates the channel because it connects to your home Wi-Fi router. 
1. Boot the Master Injector and check the Serial output to see which channel your router assigned it.
2. Hardcode that exact channel into the `#define WIFI_CHANNEL` variable on all **Relay** and **Gateway** nodes before flashing.

### Step-by-Step Deployment

**1. Flash the Display Gateway (ESP8266)**
* Board: `Generic ESP8266 Module`
* Dependencies: `Adafruit_GFX`, `Adafruit_SSD1306`
* Note down the MAC address printed to the OLED on boot.

**2. Flash the Relay Node (ESP32)**
* Board: `ESP32 Dev Module`
* Ensure the channel matches the Master. Place this physically between the Master and the Gateway.

**3. Flash the Master Injector (ESP32-C6)**
* Board: `ESP32C6 Dev Module`
* **Crucial C6 Hardware Quirk:** You must enable `USB CDC On Boot` in the Arduino IDE Tools menu, otherwise you will get zero output on the Serial monitor.
* Once booted, navigate to the IP address printed in the terminal to access the Web UI.

## 🛠️ Linux & Container Deployment Notes

If you plan to run the ESP8266 Gateway permanently attached to a headless server or thin client, you can easily bridge the Serial output into a larger automation stack. 

When passing the device into a Podman container (running Debian or Fedora), ensure you map the hardware correctly:
```bash
podman run -d --device=/dev/ttyUSB0:/dev/ttyUSB0:rwm my-python-bridge
```

## 📦 The Packet Structure
All nodes communicate using a tightly packed binary struct:

```
struct __attribute__((packed)) EspNowPocsagPacket {
  uint8_t  type;          // Protocol identifier (0x11 for POCSAG)
  uint8_t  ttl;           // Hop counter (decrements on relay)
  uint32_t msgId;         // Randomly generated ID for deduplication
  uint8_t  destMac[6];    // Final destination MAC address
  uint32_t ric;           // Pager Receiver Identification Code
  uint8_t  functional;    // POCSAG message type
  char     message[81];   // Payload array
};
```
