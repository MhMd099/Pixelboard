#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

String jsonEscape(const String& value);
String htmlEscape(const String& value);

// --- API Keys ---
extern const char* weatherApiKey;

extern bool forceWeatherUpdate;
extern String currentCity;
String currentCitySnapshot();
constexpr const char* PRIMARY_PLAYER_NAME = "NothinBL";
constexpr const char* SECONDARY_PLAYER_NAME = "Guest";
extern uint8_t snakeObstacleTarget;
extern uint8_t snakeFoodTarget;
extern uint8_t snakePowerupTarget;
extern uint8_t snakeBotCount;
extern uint8_t joystick1DeadzonePercent;
extern uint8_t joystick2DeadzonePercent;
extern uint8_t joystick3DeadzonePercent;
extern volatile uint32_t tetrisCurrentScore;
extern volatile uint32_t tetrisBoardScore;
extern volatile bool tetrisPlaying;
extern volatile uint16_t tetrisNormalFallMs;
extern volatile uint16_t tetrisSoftFallMs;
extern volatile uint16_t tetrisMinFallMs;
extern volatile uint16_t tetrisSpeedScoreStep;
extern volatile uint16_t tetrisSpeedStepMs;
extern volatile bool tetrisFastBotMode;
extern volatile uint8_t pongBotMode;
String standbyTextSnapshot();
void setStandbyText(String text);

// --- WLAN / Captive Portal ---
extern String wifiSsid;
extern String wifiPassword;
void loadWifiCredentials();
bool hasWifiCredentials();
String configuredWifiSsid();
void saveWifiCredentials(String newSsid, String newPassword);
void beginWifiConnection();
void maintainWifiConnection();
void stopCaptivePortal();
String deviceHostname();
String deviceLocalUrl();

// --- Geraete-Einstellungen ---
extern int currentDhtPin;
extern int currentDhtType;
void loadDeviceSettings();

// --- Funktionen ---
void initLittleFS();
void loadDeviceProfile();
void saveCity(String city);
void saveTheme(int themeIdx);
void saveClockStyle(int clockIdx);
void startCaptivePortal();
void setupWebServer();
void taskWebServerHandler(void * pvParameters);
#endif
