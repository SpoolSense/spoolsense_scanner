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

// main.cpp — ESP32 firmware entry point and hardware init. Manages WiFi, AP mode, NTP, display/LED/
// keypad peripherals, NFC reader selection, and the main loop that dispatches to task managers

// Global HTTP mutex for serializing WiFi HTTP requests (blocks SpoolmanManager + ApplicationManager + PrinterManager)
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

// TFT display — constructed in setup() after NVS config loads (needs driver type)
TFTManager* tftManagerPtr = nullptr;

// Always declared; only initialized if isLedEnabled() at runtime
LEDManager ledManager;

void startAPMode() {
  // Fallback when WiFi SSID not configured or connection fails — zero-CLI setup via captive portal
  uint8_t mac[6];
  WiFi.macAddress(mac);
  snprintf(g_apSSID, sizeof(g_apSSID), "SpoolSense-%02X%02X", mac[4], mac[5]);

  Serial.printf("Starting AP mode: %s\n", g_apSSID);

  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  WiFi.softAP(g_apSSID);

  // Captive portal: intercept DNS queries and redirect to config page
  dnsServer.start(53, "*", IPAddress(192,168,4,1));

  g_apModeActive = true;

  Serial.printf("AP started: %s @ 192.168.4.1\n", g_apSSID);

  auto& config = ConfigurationManager::getInstance();
  if (config.isTftEnabled() && tftManagerPtr) {
    tftManagerPtr->showError(g_apSSID);
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

  if (config.isTftEnabled() && tftManagerPtr && !config.isBambuDashboardEnabled()) {
    tftManagerPtr->showWifiConnecting();
  } else if (config.isLcdEnabled()) {
    lcdManager.updateScreen("Connecting WiFi", "");
  }

  WiFi.setHostname(config.getHostname());
  WiFi.begin(config.getWiFiSSID(), config.getWiFiPassword());
  // Keep-awake disables WIFI_PS_MIN_MODEM (Arduino default). Users on weak
  // networks report ~10 dB better RSSI with modem sleep off; costs idle current.
  WiFi.setSleep(!config.isWifiKeepAwakeEnabled());

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

    if (config.isTftEnabled() && tftManagerPtr && !config.isBambuDashboardEnabled()) {
      tftManagerPtr->showWifiConnected(WiFi.localIP().toString().c_str());
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
      if (config.isTftEnabled() && tftManagerPtr) {
        tftManagerPtr->showError("NTP FAILED");
      } else if (config.isLcdEnabled()) {
        lcdManager.updateScreen("NTP FAILED", "");
      }
    } else {
      Serial.println("Time obtained");
    }

    delay(2000);  // Display settle time before task managers start
    g_wifiWasConnected = true;
  } else {
    Serial.println("");
    Serial.println("WiFi connection failed - starting AP mode");
    startAPMode();
  }
}

void retryNTP() {
  static bool ntpObtained = false;
  static unsigned long lastAttempt = 0;

  if (ntpObtained || g_apModeActive) return;
  // Threshold check: ~Nov 2023 onward means time was synced (cheap way to detect valid UNIX time)
  if (time(nullptr) > 1700000000) { ntpObtained = true; return; }
  // 30s throttle: NTP is slow, don't hammer it
  if (millis() - lastAttempt < 30000) return;

  lastAttempt = millis();
  Serial.println("NTP: Retrying...");
  configTime(0, 0, "pool.ntp.org");
}

void checkWiFi() {
  // Skip in AP mode — no WiFi to reconnect
  if (g_apModeActive) return;

  unsigned long now = millis();
  // Throttle checks to 5s interval to avoid thrashing WiFi state machine
  if (now - g_lastWifiCheckMs < WIFI_CHECK_INTERVAL_MS) return;
  g_lastWifiCheckMs = now;

  bool connected = (WiFi.status() == WL_CONNECTED);

  if (g_wifiWasConnected && !connected) {
    // Transition from connected → disconnected
    Serial.println("WiFi: Connection lost — starting reconnection");
    auto& config = ConfigurationManager::getInstance();
    if (config.isTftEnabled() && tftManagerPtr) {
      tftManagerPtr->showText("WiFi Lost", "Reconnecting...");
    } else if (config.isLcdEnabled()) {
      lcdManager.updateScreen("WiFi Lost", "Reconnecting...");
    }
    g_wifiReconnectDelay = WIFI_RECONNECT_INITIAL_MS;
    g_lastReconnectAttemptMs = now;
    WiFi.reconnect();
    g_wifiWasConnected = false;
  } else if (!connected && !g_wifiWasConnected) {
    // Disconnected → still disconnected: exponential backoff retry
    if (now - g_lastReconnectAttemptMs >= g_wifiReconnectDelay) {
      Serial.printf("WiFi: Reconnect attempt (backoff %lums)\n", g_wifiReconnectDelay);
      WiFi.reconnect();
      g_lastReconnectAttemptMs = now;
      // Double backoff capped at 60s to avoid hammering WiFi radio
      if (g_wifiReconnectDelay < WIFI_RECONNECT_MAX_MS) {
        g_wifiReconnectDelay *= 2;
        if (g_wifiReconnectDelay > WIFI_RECONNECT_MAX_MS) {
          g_wifiReconnectDelay = WIFI_RECONNECT_MAX_MS;
        }
      }
    }
  } else if (!g_wifiWasConnected && connected) {
    // Reconnected: re-advertise mDNS (IP may have changed if DHCP renewal)
    Serial.printf("WiFi: Reconnected! IP: %s\n", WiFi.localIP().toString().c_str());
    auto& config = ConfigurationManager::getInstance();
    if (config.isTftEnabled() && tftManagerPtr) {
      tftManagerPtr->showText("WiFi OK", WiFi.localIP().toString().c_str());
    } else if (config.isLcdEnabled()) {
      lcdManager.updateScreen("WiFi OK", WiFi.localIP().toString().c_str());
    }

    // Re-initialize mDNS after reconnection — stale entries cause lookup failures
    MDNS.end();
    const char* hostname = ConfigurationManager::getInstance().getHostname();
    if (MDNS.begin(hostname)) {
      MDNS.addService("http", "tcp", HTTP_PORT);
      Serial.printf("WiFi: mDNS restarted (%s.local)\n", hostname);
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

  // ConfigurationManager MUST init before any hardware — it reads NVS to gate feature initialization
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
    // TFT display: runtime driver selection (st7789 or gc9a01) from NVS
    TFTDriver tftDriver = TFTDriver::ST7789;
    if (strcmp(config.getTftDriver(), "gc9a01") == 0) {
        tftDriver = TFTDriver::GC9A01;
    }
    tftManagerPtr = new TFTManager(tftDriver);
    tftManagerPtr->begin();
    tftManagerPtr->startTask();
    tftManagerPtr->showBoot(FIRMWARE_VERSION);
    Serial.println("TFT initialized");
  } else if (config.isLcdEnabled()) {
    // I2C LCD: custom pins to avoid conflicts with NFC/TFT SPI buses
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

  // ApplicationManager: message queue dispatcher with optional display backing
  DisplayI* activeDisplay = nullptr;
  if (config.isTftEnabled()) {
    activeDisplay = tftManagerPtr;
  } else if (config.isLcdEnabled()) {
    activeDisplay = &lcdManager;
  }
  if (!ApplicationManager::getInstance().begin(activeDisplay)) {
    Serial.println("ApplicationManager init failed - halting");
    while (1) { delay(1000); }
  }

  // Connect to WiFi
  initWiFi();

  // Global mutex: guards all HTTP clients (SpoolmanManager, ApplicationManager ASSIGN_SPOOL, PrinterManager)
  g_httpMutex = xSemaphoreCreateMutex();
  if (g_httpMutex == nullptr) {
    Serial.println("Failed to create HTTP mutex - halting");
    while (1) { delay(1000); }
  }

  // Skip network-dependent managers in AP mode (config-only)
  if (!g_apModeActive) {
    // Initialize SpoolmanManager
    if (!SpoolmanManager::getInstance().begin(g_httpMutex)) {
      Serial.println("SpoolmanManager init failed - continuing without Spoolman");
    }

    // Initialize HomeAssistantManager
    if (!HomeAssistantManager::getInstance().begin()) {
      Serial.println("HomeAssistantManager init failed - continuing without HA");
    }

    // Load automation mode from config: SELF_DIRECTED (auto tag updates) vs CONTROLLED_BY_HA (manual)
    {
      uint8_t mode = config.getAutomationMode();
      ApplicationManager::getInstance().setAutomationMode(static_cast<AutomationMode>(mode));
      Serial.printf("Automation mode: %s\n",
                    mode == 0 ? "SELF_DIRECTED" : "CONTROLLED_BY_HA");
    }
  } else {
    Serial.println("AP mode active - skipping Spoolman, HA, automation init");
  }

  // NFC reader selection: PN532 (ISO14443A only) vs PN5180 (multi-format default)
  const char* nfcReader = config.getNfcReader();
  if (strcmp(nfcReader, "pn532") == 0) {
    Serial.println("NFC reader: PN532 (ISO14443A only)");
    NFCManager::getInstance().setConnection(new HardwareNFCConnectionPN532());
  } else {
    Serial.printf("NFC reader: %s (default)\n", nfcReader);
  }

  // Initialize NFCManager
  if (!NFCManager::getInstance().begin()) {
    Serial.println("NFCManager init failed - continuing without NFC");
    if (config.isTftEnabled() && tftManagerPtr) {
      tftManagerPtr->showError("NFC FAILED");
    } else if (config.isLcdEnabled()) {
      lcdManager.updateScreen("NFC FAILED", "");
    }
  } else {
    // Start NFC scan task
    NFCManager::getInstance().startScanTask();
  }

  if (!g_apModeActive) {
    // Start network task managers
    SpoolmanManager::getInstance().startTask();

    Serial.printf("Setup: HA config before startTask: enabled=%s host='%s' host_len=%u port=%u user_set=%s\n",
                  config.getHAEnabled() ? "true" : "false",
                  config.getHAMqttHost(),
                  static_cast<unsigned>(strlen(config.getHAMqttHost())),
                  static_cast<unsigned>(config.getHAMqttPort()),
                  strlen(config.getHAMqttUser()) > 0 ? "true" : "false");
    HomeAssistantManager::getInstance().startTask();

    // PrusaLink printer polling: detects print start/end for spool deduction
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
    if (tftManagerPtr && !config.isBambuDashboardEnabled()) {
      tftManagerPtr->showReady();
    } else if (tftManagerPtr && config.isBambuDashboardEnabled()) {
      // Show cached dashboard or AMS Ready after full boot (TFT task is running)
      const TrayDashboardState& cached = ApplicationManager::getInstance().getTrayDashboardState();
      if (cached.has_data) {
        tftManagerPtr->showTrayDashboard(cached);
      } else {
        tftManagerPtr->showText("SpoolSense", "AMS Ready");
      }
    }
  } else if (config.isLcdEnabled()) {
    ApplicationManager::getInstance().showStatusScreen();
  }

  if (config.isLedEnabled()) {
    ledManager.showReady();
    ledManager.startTask();  // Async LED task — non-blocking calls from here
  }

  // HTTP server: available in both STA and AP modes (tag reader/writer web UI)
  if (WiFi.status() == WL_CONNECTED || g_apModeActive) {
    WebServerManager::getInstance().begin(g_apModeActive);
    WebServerManager::getInstance().setDisplay(activeDisplay);
  }

  Serial.println("=== Setup complete ===");
}

void loop() {
  // AP mode: respond to DNS queries (all redirected to config page)
  if (g_apModeActive) {
    dnsServer.processNextRequest();
  }

  // Dispatch all queued application events (tag detections, print start/end, etc.)
  ApplicationManager::getInstance().processMessages();

  // HTTP server: handle web UI requests (reader/writer pages, config, OTA)
  WebServerManager::getInstance().handleClient();

  // Keypad polling: 4x3 matrix for ASSIGN_SPOOL tool number entry
  if (ConfigurationManager::getInstance().isKeypadEnabled()) {
    InputManager::getInstance().poll();
  }

  // Watchdog: detect WiFi drops and reconnect with exponential backoff
  checkWiFi();

  // Fallback: retry NTP sync if boot-time attempt failed (non-blocking 30s interval)
  retryNTP();

  // NFC scanning and other async work delegated to FreeRTOS tasks — 10ms soft delay prevents loop busywaiting
  delay(10);
}
