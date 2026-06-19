#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// --- Netzwerkeinstellungen ---
extern const char* ssid;
extern const char* password;

// --- API Keys ---
extern const char* weatherApiKey;

// --- Google Sheets (für später) ---
extern const char* spreadsheetId;
extern const char* PROJECT_ID;
extern const char* CLIENT_EMAIL;
extern const char* PRIVATE_KEY;

// Hier muss es auch deklariert sein
extern bool forceWeatherUpdate; 
extern String currentCity;
extern String currentUser;
// --- Funktionen ---
void initLittleFS();
// Lade Default-Daten beim Start, z.B. für einen leeren User
void loadConfigForUser(String user);
void saveConfig(String user, String city);
void setupWebServer();
void taskWebServerHandler(void * pvParameters);

#endif