#include <WiFi.h>
#include <DNSServer.h>
#include <time.h>
#include <ESPmDNS.h>
#include <cstring>

#include "ConfigurationManager.h"
#include "ApplicationManager.h"
#include "NFCManager.h"
#include "SpoolmanManager.h"
#include "HomeAssistantManager.h"
#include "DisplayI.h"
#include "LCDManager.h"
#include "TFTManager.h"
#include "LEDManager.h"
#include "WebServerManager.h"
#include "PrinterManager.h"
#include "PrusaLinkStrategy.h"
#include "InputManager.h"
#include "HardwareNFCConnectionPN532.h"
#include "BoardPins.h"
#include <Wire.h>

// Global HTTP mutex for serializing WiFi HTTP requests
SemaphoreHandle_t g_httpMutex = nullptr;

// AP mode state
bool g_apModeActive = false;
static DNSServer dnsServer;
char g_apSSID[24] = {0};

// PrusaLink strategy (file-scope so it outlives setup)
static PrusaLinkStrategy prusaLinkStrategy;

// WiFi reconnection watchdog state
static bool g_wifiWasConnected = false;
static unsigned long g_lastWifiCheckMs = 0;
static unsigned long g_wifiReconnectDelay = 5000;
static unsigned long g_lastReconnectAttemptMs = 0;
static constexpr unsigned long WIFI_CHECK_INTERVAL_MS = 5000;
static constexpr unsigned long WIFI_RECONNECT_INITIAL_MS = 5000;
static constexpr unsigned long WIFI_RECONNECT_MAX_MS = 60000;
static constexpr uint16_t HTTP_PORT = 80;

// Always declared; only initialized if isLcdEnabled() at runtime
LCDManager lcdManager(0x27, 16, 2);

// TFT display — replaces LCD when enabled via NVS
TFTManager tftManager;

// Always declared; only initialized if isLedEnabled() at runtime
LEDManager ledManager;

void startAPMode() {
  // Generate SSID from last 4 hex digits of MAC
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(g_apSSID, sizeof(g_apSSID), "SpoolSense-%02X%02X", mac[4], mac[5]);

  Serial.printf("Starting AP mode: %s\n", g_apSSID);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  WiFi.softAP(g_apSSID);

  // Captive portal DNS — redirect all lookups to our IP
  dnsServer.start(53, "*", IPAddress(192,168,4,1));

  g_apModeActive = true;

  Serial.printf("AP started: %s @ 192.168.4.1\n", g_apSSID);

  auto& config = ConfigurationManager::getInstance();
  if (config.isTftEnabled()) {
    tftManager.showError(g_apSSID);
  } else if (config.isLcdEnabled()) {
    lcdManager.updateScreen(g_apSSID, "Go to 192.168.4.1");
  }
  if (config.isLedEnabled()) {
    ledManager.showWifiFailed();  // yellow/warning state
  }
}

void initWiFi() {
  auto& config = ConfigurationManager::getInstance();

  if (strlen(config.getWiFiSSID()) == 0) {
    Serial.println("WiFi SSID not configured - starting AP mode");
    startAPMode();
    return;
  }

  Serial.print("Connecting to WiFi: ");
  Serial.println(config.getWiFiSSID());

  if (config.isTftEnabled()) {
    tftManager.showWifiConnecting();
  } else if (config.isLcdEnabled()) {
    lcdManager.updateScreen("Connecting WiFi", "");
  }

  WiFi.begin(config.getWiFiSSID(), config.getWiFiPassword());

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.print("WiFi connected! IP: ");
    Serial.println(WiFi.localIP());

    if (config.isTftEnabled()) {
      tftManager.showWifiConnected(WiFi.localIP().toString().c_str());
    } else if (config.isLcdEnabled()) {
      lcdManager.updateScreen("WiFi OK", WiFi.localIP().toString().c_str());
    }

    if (config.isLedEnabled()) {
      ledManager.showWifiConnected();
    }

    Serial.println("Setting up NTP...");
    configTime(0, 0, "pool.ntp.org");
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
      Serial.println("Failed to obtain time");
      if (config.isTftEnabled()) {
        tftManager.showError("NTP FAILED");
      } else if (config.isLcdEnabled()) {
        lcdManager.updateScreen("NTP FAILED", "");
      }
    } else {
      Serial.println("Time obtained");
    }

    delay(2000);
    g_wifiWasConnected = true;
  } else {
    Serial.println("");
    Serial.println("WiFi connection failed - starting AP mode");
    startAPMode();
  }
}

void checkWiFi() {
  // Skip in AP mode — no WiFi to reconnect
  if (g_apModeActive) return;

  unsigned long now = millis();
  if (now - g_lastWifiCheckMs < WIFI_CHECK_INTERVAL_MS) return;
  g_lastWifiCheckMs = now;

  bool connected = (WiFi.status() == WL_CONNECTED);

  if (g_wifiWasConnected && !connected) {
    // WiFi just dropped
    Serial.println("WiFi: Connection lost — starting reconnection");
    auto& config = ConfigurationManager::getInstance();
    if (config.isTftEnabled()) {
      tftManager.showText("WiFi Lost", "Reconnecting...");
    } else if (config.isLcdEnabled()) {
      lcdManager.updateScreen("WiFi Lost", "Reconnecting...");
    }
    g_wifiReconnectDelay = WIFI_RECONNECT_INITIAL_MS;
    g_lastReconnectAttemptMs = now;
    WiFi.reconnect();
    g_wifiWasConnected = false;
  } else if (!connected && !g_wifiWasConnected) {
    // Still disconnected — retry with backoff
    if (now - g_lastReconnectAttemptMs >= g_wifiReconnectDelay) {
      Serial.printf("WiFi: Reconnect attempt (backoff %lums)\n", g_wifiReconnectDelay);
      WiFi.reconnect();
      g_lastReconnectAttemptMs = now;
      if (g_wifiReconnectDelay < WIFI_RECONNECT_MAX_MS) {
        g_wifiReconnectDelay *= 2;
        if (g_wifiReconnectDelay > WIFI_RECONNECT_MAX_MS) {
          g_wifiReconnectDelay = WIFI_RECONNECT_MAX_MS;
        }
      }
    }
  } else if (!g_wifiWasConnected && connected) {
    // WiFi just reconnected
    Serial.printf("WiFi: Reconnected! IP: %s\n", WiFi.localIP().toString().c_str());
    auto& config = ConfigurationManager::getInstance();
    if (config.isTftEnabled()) {
      tftManager.showText("WiFi OK", WiFi.localIP().toString().c_str());
    } else if (config.isLcdEnabled()) {
      lcdManager.updateScreen("WiFi OK", WiFi.localIP().toString().c_str());
    }

    // Re-initialize mDNS (IP may have changed)
    MDNS.end();
    if (MDNS.begin("spoolsense")) {
      MDNS.addService("http", "tcp", HTTP_PORT);
      Serial.println("WiFi: mDNS restarted (spoolsense.local)");
    } else {
      Serial.println("WiFi: mDNS restart failed — reachable by IP only");
    }

    g_wifiReconnectDelay = WIFI_RECONNECT_INITIAL_MS;
    g_wifiWasConnected = true;
  }
}

void setup() {
  delay(1000);
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== Starting setup ===");

  // Initialize ConfigurationManager FIRST — reads NVS to determine which
  // optional hardware features are enabled before initializing any peripherals
  if (!ConfigurationManager::getInstance().begin()) {
    Serial.println("ConfigurationManager init failed - halting");
    while (1) { delay(1000); }
  }

  auto& config = ConfigurationManager::getInstance();

  if (config.isLedEnabled()) {
    ledManager.begin(PIN_STATUS_LED);
    ledManager.showBooting();
  }

  if (config.isTftEnabled()) {
    // TFT display — mutually exclusive with LCD on WROOM
    tftManager.begin();
    tftManager.startTask();
    tftManager.showBoot(FIRMWARE_VERSION);
    Serial.println("TFT initialized");
  } else if (config.isLcdEnabled()) {
    // Initialize I2C with custom pins for LCD
    Wire.begin(PIN_LCD_SDA, PIN_LCD_SCL);
    Serial.println("I2C initialized");

    lcdManager.begin();
    lcdManager.startTask();
    lcdManager.updateScreen("Initializing...", "");
    Serial.println("LCD initialized");

    lcdManager.setScreenTimeoutMs(config.getLcdTimeoutMs());
  }

  if (config.isKeypadEnabled()) {
    InputManager::getInstance().begin();
  }

  // Initialize ApplicationManager (message queue) with display reference
  DisplayI* activeDisplay = nullptr;
  if (config.isTftEnabled()) {
    activeDisplay = &tftManager;
  } else if (config.isLcdEnabled()) {
    activeDisplay = &lcdManager;
  }
  if (!ApplicationManager::getInstance().begin(activeDisplay)) {
    Serial.println("ApplicationManager init failed - halting");
    while (1) { delay(1000); }
  }

  // Connect to WiFi
  initWiFi();

  // Create global HTTP mutex for serializing HTTP requests
  g_httpMutex = xSemaphoreCreateMutex();
  if (g_httpMutex == nullptr) {
    Serial.println("Failed to create HTTP mutex - halting");
    while (1) { delay(1000); }
  }

  // Skip network-dependent managers in AP mode
  if (!g_apModeActive) {
    // Initialize SpoolmanManager
    if (!SpoolmanManager::getInstance().begin(g_httpMutex)) {
      Serial.println("SpoolmanManager init failed - continuing without Spoolman");
    }

    // Initialize HomeAssistantManager
    if (!HomeAssistantManager::getInstance().begin()) {
      Serial.println("HomeAssistantManager init failed - continuing without HA");
    }

    // Load automation mode from config
    {
      uint8_t mode = config.getAutomationMode();
      ApplicationManager::getInstance().setAutomationMode(static_cast<AutomationMode>(mode));
      Serial.printf("Automation mode: %s\n",
                    mode == 0 ? "SELF_DIRECTED" : "CONTROLLED_BY_HA");
    }
  } else {
    Serial.println("AP mode active - skipping Spoolman, HA, automation init");
  }

  // Select NFC reader based on NVS config
  const char* nfcReader = config.getNfcReader();
  if (strcmp(nfcReader, "pn532") == 0) {
    Serial.println("NFC reader: PN532 (ISO14443A only)");
    NFCManager::getInstance().setConnection(new HardwareNFCConnectionPN532());
  } else {
    Serial.printf("NFC reader: %s (default)\n", nfcReader);
  }

  // Initialize NFCManager
  if (!NFCManager::getInstance().begin()) {
    Serial.println("NFCManager init failed - halting");
    if (config.isLcdEnabled()) {
      lcdManager.updateScreen("NFC FAILED", "");
    }
    while (1) { delay(1000); }
  }

  // Start NFC scan task
  NFCManager::getInstance().startScanTask();

  if (!g_apModeActive) {
    // Start SpoolmanManager task
    SpoolmanManager::getInstance().startTask();

    // Start HomeAssistantManager task
    Serial.printf("Setup: HA config before startTask: enabled=%s host='%s' host_len=%u port=%u user_set=%s\n",
                  config.getHAEnabled() ? "true" : "false",
                  config.getHAMqttHost(),
                  static_cast<unsigned>(strlen(config.getHAMqttHost())),
                  static_cast<unsigned>(config.getHAMqttPort()),
                  strlen(config.getHAMqttUser()) > 0 ? "true" : "false");
    HomeAssistantManager::getInstance().startTask();

    // Start PrusaLink printer polling (if configured)
    if (config.isPrusaLinkEnabled()) {
      PrinterManager::getInstance().begin();
      prusaLinkStrategy.setHttpMutex(g_httpMutex);
      PrinterManager::getInstance().setStrategy(&prusaLinkStrategy);
      PrinterManager::getInstance().startPollingTask();
      Serial.println("PrusaLink integration enabled");
    } else {
      Serial.println("PrusaLink integration disabled (not configured)");
    }
  }

  if (config.isTftEnabled()) {
    tftManager.showReady();
  } else if (config.isLcdEnabled()) {
    ApplicationManager::getInstance().showStatusScreen();
  }

  if (config.isLedEnabled()) {
    ledManager.showReady();  // NFC + Spoolman + HA + scanner all initialized
    ledManager.startTask();  // Start async LED task — all LED calls are non-blocking from here
  }

  // Start HTTP server (both STA and AP mode)
  if (WiFi.status() == WL_CONNECTED || g_apModeActive) {
    WebServerManager::getInstance().begin(g_apModeActive);
    WebServerManager::getInstance().setDisplay(activeDisplay);
  }

  Serial.println("=== Setup complete ===");
}

void loop() {
  // Process captive portal DNS in AP mode
  if (g_apModeActive) {
    dnsServer.processNextRequest();
  }

  // Process any pending messages for the application
  ApplicationManager::getInstance().processMessages();

  // Process HTTP requests for the tag writer web UI
  WebServerManager::getInstance().handleClient();

  // Poll keypad for key presses (if enabled)
  if (ConfigurationManager::getInstance().isKeypadEnabled()) {
    InputManager::getInstance().poll();
  }

  // WiFi reconnection watchdog (non-AP mode only, throttled to WIFI_CHECK_INTERVAL_MS)
  checkWiFi();

  // LCD and NFC scanning are handled by their own tasks
  delay(10);
}
