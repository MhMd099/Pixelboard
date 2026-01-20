#include <Arduino.h>

#include <WiFi.h>

#include "time.h"

#include <FastLED.h>

#include <LEDMatrix.h>

#include <LEDText.h>

#include <FontMatrise.h>

// Hardware-Konfiguration bleibt gleich

#define LED_PIN_OBEN 26

#define LED_PIN_UNTEN 25

#define COLOR_ORDER GRB

#define CHIPSET WS2812B

#define MATRIX_WIDTH 32

#define MATRIX_HEIGHT 8

#define MATRIX_TYPE VERTICAL_ZIGZAG_MATRIX

// WiFi & NTP (Bitte deine Daten eintragen)

const char *ssid = "Nothin";

const char *password = "nothin099";

cLEDMatrix<MATRIX_WIDTH, -MATRIX_HEIGHT, MATRIX_TYPE> ledsOben;

cLEDMatrix<-MATRIX_WIDTH, MATRIX_HEIGHT, MATRIX_TYPE> ledsUnten;

cLEDText AnzeigeOben;

cLEDText AnzeigeUnten;

// Wir fügen das Steuerzeichen für Scrollen direkt in den String ein

char datumBuffer[40];

char zeitBuffer[40];

void setup()
{

  Serial.begin(115200);

  FastLED.addLeds<CHIPSET, LED_PIN_OBEN, COLOR_ORDER>(ledsOben[0], ledsOben.Size());

  FastLED.addLeds<CHIPSET, LED_PIN_UNTEN, COLOR_ORDER>(ledsUnten[0], ledsUnten.Size());

  FastLED.setBrightness(15);

  FastLED.clear(true);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
  }

  configTime(3600, 3600, "pool.ntp.org");

  AnzeigeOben.SetFont(MatriseFontData);

  AnzeigeOben.Init(&ledsOben, ledsOben.Width(), AnzeigeOben.FontHeight() + 1, 0, 0);

  AnzeigeUnten.SetFont(MatriseFontData);

  AnzeigeUnten.Init(&ledsUnten, ledsUnten.Width(), AnzeigeUnten.FontHeight() + 1, 0, 0);

  AnzeigeOben.SetTextColrOptions(COLR_RGB | COLR_SINGLE, 0xff, 0xff, 0xff);

  AnzeigeUnten.SetTextColrOptions(COLR_RGB | COLR_SINGLE, 0x00, 0xff, 0xff);
}

void loop()
{

  struct tm timeinfo;

  if (getLocalTime(&timeinfo))
  {

    // Wir bauen das Steuerzeichen für "Scroll Links" direkt an den Anfang des Buffers

    // Das Steuerzeichen wird durch das Makro EFFECT_SCROLL_LEFT definiert

    sprintf(datumBuffer, "\x02    %02d.%02d.%04d    ", timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900);

    sprintf(zeitBuffer, "\x02    %02d:%02d:%02d    ", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  }

  // UpdateText gibt -1 zurück, wenn die Nachricht zu Ende gescrollt ist

  if (AnzeigeOben.UpdateText() == -1)
  {

    AnzeigeOben.SetText((unsigned char *)datumBuffer, strlen(datumBuffer));
  }

  if (AnzeigeUnten.UpdateText() == -1)
  {

    AnzeigeUnten.SetText((unsigned char *)zeitBuffer, strlen(zeitBuffer));
  }


  

  FastLED.show();

  delay(30);
}