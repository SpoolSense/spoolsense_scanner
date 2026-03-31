#ifndef WEB_SERVER_MANAGER_H
#define WEB_SERVER_MANAGER_H

#ifndef NATIVE_TEST
#include <WebServer.h>
#endif

class DisplayI;

class WebServerManager {
public:
    static WebServerManager& getInstance();

    // Call once in setup() after WiFi is connected or AP mode started.
    // Registers routes, starts mDNS as "spoolsense" (STA only).
    bool begin(bool apMode = false, uint16_t port = 80);

    // Call from loop() — processes pending HTTP requests.
    void handleClient();
    void setDisplay(DisplayI* display) { _display = display; }

private:
    WebServerManager() = default;
    WebServerManager(const WebServerManager&) = delete;
    WebServerManager& operator=(const WebServerManager&) = delete;

#ifndef NATIVE_TEST
    WebServer _server{80};
    bool _initialized = false;
    bool _apMode = false;

    // Page handlers
    void handleLanding();
    void handleReader();
    void handleOpenPrintTagWriter();
    void handleTigerTagWriter();
    void handleOpenTag3DWriter();
    void handleSharedCSS();
    void handleSharedJS();
    void handleOpenPrintTagLogo();
    void handleTigerTagLogo();
    void handleOpenTag3DLogo();
    void handleUpdatePage();
    void handleConfigPage();

    // API handlers
    void handleApiStatus();
    void handleApiWriteTag();
    void handleApiFormatTag();
    void handleApiWriteTigerTag();
    void handleApiWriteOpenTag3D();
    void handleApiVersion();
    void handleApiUploadFirmwareComplete();
    void handleApiUploadFirmwareChunk();
    void handleApiUpdateFromUrl();
    void handleApiOtaStatus();
    void handleApiGetConfig();
    void handleApiPostConfig();
    void handleApiDiagnostics();
    void handleTroubleshootingPage();
    void handleUIDRegistrationPage();
    void handleApiRegisterUid();
    void handleApiSpoolmanSpools();
    void handleApiSpoolmanLink();

    // OTA download state
    static void otaDownloadTask(void* param);
    enum class OtaState : uint8_t { IDLE, DOWNLOADING, FLASHING, SUCCESS, FAILED };
    volatile OtaState _otaState = OtaState::IDLE;
    char _otaUrl[512] = {0};
    char _otaError[64] = {0};
    volatile uint8_t _otaProgress = 0;

    DisplayI* _display = nullptr;

    void sendError(int code, const char* msg);
#endif
};

#endif // WEB_SERVER_MANAGER_H
