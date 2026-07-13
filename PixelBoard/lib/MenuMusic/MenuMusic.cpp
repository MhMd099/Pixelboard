#include "MenuMusic.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <AudioFileSourceLittleFS.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutput.h>
#include <AudioLogger.h>

#include "SoundUtils.h"
#include "ConfigStorage.h"

extern volatile int aktiverTask;

namespace {
const char* MENU_TRACK = "/aria_math.mp3";

class AudioOutputMixer : public AudioOutput {
public:
    bool SetRate(int hz) override {
        AudioOutput::SetRate(hz);
        return hz == 44100;
    }

    bool SetBitsPerSample(int bits) override {
        AudioOutput::SetBitsPerSample(bits);
        return bits == 16;
    }

    bool SetChannels(int channelsValue) override {
        AudioOutput::SetChannels(channelsValue);
        return channelsValue == 1 || channelsValue == 2;
    }

    bool begin() override {
        clearMenuMusicBuffer();
        setMenuMusicEnabled(true);
        return true;
    }

    bool ConsumeSample(int16_t sample[2]) override {
        int32_t mixed = sample[LEFTCHANNEL];
        if (channels == 2) mixed = (mixed + sample[RIGHTCHANNEL]) / 2;
        return pushMenuMusicSample((int16_t)mixed);
    }

    bool stop() override {
        setMenuMusicEnabled(false);
        return true;
    }
};
}

void taskMenuMusic(void *pvParameters) {
    audioLogger = &Serial;
    AudioFileSourceLittleFS file;
    AudioGeneratorMP3 decoder;
    AudioOutputMixer output;
    bool running = false;
    bool pausedForTask = false;
    bool missingLogged = false;

    for (;;) {
        if (aktiverTask != -1) {
            if (running && !pausedForTask) {
                // Keep decoder, file position and PCM buffer intact. This makes
                // the menu track continue at the audible position after the app.
                setMenuMusicEnabled(false);
                pausedForTask = true;
                Serial.println("Menue-Musik pausiert.");
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (running && pausedForTask) {
            setMenuMusicEnabled(true);
            pausedForTask = false;
            Serial.println("Menue-Musik fortgesetzt.");
        }

        if (!running) {
            ConfigStorageGuard storageGuard;
            if (!storageGuard.ok()) {
                setMenuMusicEnabled(false);
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }
            if (!LittleFS.exists(MENU_TRACK) || !file.open(MENU_TRACK)) {
                if (!missingLogged) {
                    Serial.println("Menue-Musik fehlt in LittleFS: /aria_math.mp3");
                    missingLogged = true;
                }
                setMenuMusicEnabled(false);
                vTaskDelay(pdMS_TO_TICKS(1500));
                continue;
            }

            missingLogged = false;
            Serial.printf("Menue-MP3 gefunden: %u Byte\n", (unsigned int)file.getSize());
            clearMenuMusicBuffer();
            setMenuMusicEnabled(true);
            running = decoder.begin(&file, &output);
            if (!running) {
                file.close();
                setMenuMusicEnabled(false);
                clearMenuMusicBuffer();
                Serial.println("MP3-Decoder konnte Menue-Musik nicht starten.");
                vTaskDelay(pdMS_TO_TICKS(500));
                continue;
            }
            Serial.println("Menue-Musik gestartet.");
        }

        bool decoderRunning = false;
        {
            ConfigStorageGuard storageGuard;
            if (storageGuard.ok())
                decoderRunning = decoder.loop();
        }
        if (!decoderRunning) {
            ConfigStorageGuard storageGuard;
            decoder.stop();
            file.close();
            running = false;
            pausedForTask = false;
            clearMenuMusicBuffer();
            vTaskDelay(pdMS_TO_TICKS(30));
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
}
