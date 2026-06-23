#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

WebServer server(80);

// Variablen für die geladenen Netzdaten
String ssid = "";
String password = "";

const char* configPath = "/wlan_config.json";

//===================================================================
// LittleFS: Konfiguration laden und speichern
//===================================================================
bool loadWLANConfig() {
    if (!LittleFS.exists(configPath)) {
        Serial.println("Keine Konfigurationsdatei gefunden.");
        return false;
    }

    File configFile = LittleFS.open(configPath, "r");
    if (!configFile) {
        Serial.println("Fehler beim Öffnen der Konfigurationsdatei.");
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, configFile);
    configFile.close();

    if (error) {
        Serial.println("Fehler beim Parsen der JSON-Datei.");
        return false;
    }

    ssid = doc["ssid"].as<String>();
    password = doc["password"].as<String>();
    
    Serial.printf("Daten geladen -> SSID: %s\n", ssid.c_str());
    return true;
}

bool saveWLANConfig(String qsid, String qpass) {
    File configFile = LittleFS.open(configPath, "w");
    if (!configFile) {
        Serial.println("Fehler beim Schreiben der Konfigurationsdatei.");
        return false;
    }

    JsonDocument doc;
    doc["ssid"] = qsid;
    doc["password"] = qpass;

    if (serializeJson(doc, configFile) == 0) {
        Serial.println("Fehler beim Serialisieren der JSON-Daten.");
        configFile.close();
        return false;
    }

    configFile.close();
    Serial.println("WLAN-Daten erfolgreich in LittleFS gespeichert!");
    return true;
}

//===================================================================
// Webserver: HTML Oberfläche & Formular-Auswertung
//===================================================================
void handleRoot() {
    String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>Pixelboard WLAN Setup</title>";
    html += "<style>body{font-family:Arial; background:#121212; color:#fff; text-align:center; padding:50px;}";
    html += "input[type='text'], input[type='password']{width:80%; max-width:300px; padding:10px; margin:10px 0; border:none; border-radius:5px;}";
    html += "input[type='submit']{background:#00f5d4; color:#000; padding:10px 20px; border:none; border-radius:5px; cursor:pointer; font-weight:bold;}";
    html += "form{background:#1e1e1e; padding:20px; display:inline-block; border-radius:10px; box-shadow: 0 4px 10px rgba(0,0,0,0.5);}</style>";
    html += "</head><body>";
    html += "<h2>👾 Pixelboard Konfiguration 👾</h2>";
    html += "<p>Gib die Zugangsdaten deines Heimnetzwerks ein:</p>";
    html += "<form action='/save' method='POST'>";
    html += "SSID:<br><input type='text' name='ssid' placeholder='Netzwerkname' required><br>";
    html += "Passwort:<br><input type='password' name='password' placeholder='WLAN Passwort' required><br><br>";
    html += "<input type='submit' value='Speichern & Neustarten'>";
    html += "</form></body></html>";
    
    server.send(200, "text/html", html);
}

void handleSave() {
    if (server.hasArg("ssid") && server.hasArg("password")) {
        String req_ssid = server.arg("ssid");
        String req_pass = server.arg("password");

        String response = "<html><body><h2>Daten empfangen!</h2><p>Der ESP32 startet neu und versucht sich mit '" + req_ssid + "' zu verbinden.</p></body></html>";
        server.send(200, "text/html", response);
        delay(1000);

        // Daten sichern
        if (saveWLANConfig(req_ssid, req_pass)) {
            ESP.restart(); // Hardware-Reset auslösen, um neu zu booten
        }
    } else {
        server.send(400, "text/plain", "Fehlerhafte Anfrage.");
    }
}

//===================================================================
// Setup & Hauptschleife
//===================================================================
void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n--- Start Testprojekt: WLAN-Manager ---");

    // LittleFS initialisieren
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount-Fehler!");
        return;
    }

    // Versuche Konfiguration zu laden
    bool hasConfig = loadWLANConfig();

    if (hasConfig && ssid.length() > 0) {
        Serial.printf("Verbinde mit Heimnetzwerk: %s ...\n", ssid.c_str());
        WiFi.mode(WIFI_STA);
        WiFi.begin(ssid.c_str(), password.c_str());

        // Max. 10 Sekunden auf Verbindung warten
        int counter = 0;
        while (WiFi.status() != WL_CONNECTED && counter < 20) {
            delay(500);
            Serial.print(".");
            counter++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\nErfolgreich verbunden!");
            Serial.print("IP-Adresse im Heimnetz: ");
            Serial.println(WiFi.localIP());
            return; // Setup beenden, wir sind online!
        } else {
            Serial.println("\nVerbindung fehlgeschlagen! Wechsel in Access Point Modus.");
        }
    }

    // FALLBACK: Kein Netz gefunden oder keine Config -> Access Point starten
    Serial.println("Starte eigenen Access Point 'Pixelboard-Setup'...");
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Pixelboard-Setup", ""); // Offenes Netz ohne Passwort für die Einrichtung

    Serial.print("AP IP-Adresse: ");
    Serial.println(WiFi.softAPIP());

    // Webserver-Routen definieren
    server.on("/", HTTP_GET, handleRoot);
    server.on("/save", HTTP_POST, handleSave);
    server.begin();
    Serial.println("Webserver läuft.");
}

void loop() {
    // Der Webserver muss nur laufen, wenn wir im AP-Modus sind oder Anfragen verarbeiten
    server.handleClient();
    delay(2);
}