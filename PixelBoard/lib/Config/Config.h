#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

String jsonEscape(const String& value);
String htmlEscape(const String& value);

// --- API Keys ---
extern const char* weatherApiKey;

extern bool forceWeatherUpdate;
extern String currentCity;
extern String currentUser;
extern String pongLeftUser;
extern String pongRightUser;
extern String pacmanP1User;
extern String pacmanP2User;
String currentCitySnapshot();
String currentUserSnapshot();
String pongLeftUserSnapshot();
String pongRightUserSnapshot();
String pacmanP1UserSnapshot();
String pacmanP2UserSnapshot();
extern bool pacmanGhostManual;
extern uint8_t snakeObstacleTarget;
extern uint8_t snakeFoodTarget;
extern uint8_t snakePowerupTarget;
extern uint8_t joystick1DeadzonePercent;
extern uint8_t joystick2DeadzonePercent;
extern uint8_t joystick3DeadzonePercent;
extern uint8_t pongBotDifficulty;
extern volatile uint32_t tetrisCurrentScore;
extern volatile uint32_t tetrisBoardScore;
extern volatile uint32_t tetrisLastScore;
extern volatile bool tetrisPlaying;
extern volatile uint16_t tetrisNormalFallMs;
extern volatile uint16_t tetrisSoftFallMs;
extern volatile uint16_t tetrisMinFallMs;
extern volatile uint16_t tetrisSpeedScoreStep;
extern volatile uint16_t tetrisSpeedStepMs;

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
void saveSnakeHighScore(String user, int score);
int getSnakeHighScore(String user);
void savePongHighScore(String user, int score);
int getPongHighScore(String user);
void savePacmanHighScore(String user, int score);
int getPacmanHighScore(String user);
void saveTetrisHighScore(String user, int score);
int getTetrisHighScore(String user);

struct PlayerData { String name; int score; };
void getTopScores(PlayerData* list, int& count);
void getTopSnakeScores(PlayerData* list, int& count);
void getTopPongScores(PlayerData* list, int& count);
void getTopPacmanScores(PlayerData* list, int& count);
void getTopTetrisScores(PlayerData* list, int& count);

#endif
