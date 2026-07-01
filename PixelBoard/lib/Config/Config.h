#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// --- API Keys ---
extern const char* weatherApiKey;

extern bool forceWeatherUpdate;
extern String currentCity;
extern String currentUser;

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

// --- Geraete-Einstellungen ---
extern int currentDhtPin;
extern int currentDhtType;
void loadDeviceSettings();

// --- Funktionen ---
void initLittleFS();
void loadConfigForUser(String user);
void ensureUserExists(String user);
void saveConfig(String user, String city);
void saveTheme(String user, int themeIdx);
void saveClockStyle(String user, int clockIdx);
void startCaptivePortal();
void setupWebServer();
void taskWebServerHandler(void * pvParameters);
void saveHighScore(String user, int score);
int getHighScore(String user);
void printTopThree();

struct PlayerData { String name; int score; };
void getTopScores(PlayerData* list, int& count);

#endif
