#include "GameRuntime.h"

#include "HardwareUtils.h"
#include "Joystick.h"
#include "SoundUtils.h"
#include "RuntimeHealth.h"

static const uint32_t PAUSE_HOLD_MS = 5000UL;

GameRuntime::GameRuntime()
    : pauseLatched(false), frameClockStarted(false), lastFrameWake(0),
      resumeState(GAME_PLAYING) {
    holdStartedMs[0] = 0;
    holdStartedMs[1] = 0;
}

void GameRuntime::begin() {
    registerCurrentTaskWithWatchdog();
    holdStartedMs[0] = 0;
    holdStartedMs[1] = 0;
    pauseLatched = false;
    frameClockStarted = false;
    resumeState = GAME_PLAYING;
}

GameControlEvents GameRuntime::poll(Joystick& primary, Joystick& secondary) {
    bool primaryDouble = primary.doppelklickZaehler > 0;
    bool secondaryDouble = secondary.doppelklickZaehler > 0;
    GameControlEvents events = {
        {primary.readXPercent(), primary.readYPercent(), primary.isPressed(),
         primary.einfacherKlickZaehler > 0, primaryDouble, primary.langKlickZaehler > 0},
        {secondary.readXPercent(), secondary.readYPercent(), secondary.isPressed(),
         secondary.einfacherKlickZaehler > 0, secondaryDouble, secondary.langKlickZaehler > 0},
        primaryDouble || secondaryDouble,
        primaryDouble,
        secondaryDouble,
        false
    };
    primary.doppelklickZaehler = 0;
    secondary.doppelklickZaehler = 0;
    primary.einfacherKlickZaehler = 0;
    secondary.einfacherKlickZaehler = 0;
    primary.langKlickZaehler = 0;
    secondary.langKlickZaehler = 0;

    Joystick* sticks[2] = {&primary, &secondary};
    uint32_t now = millis();
    bool anyPressed = false;
    for (uint8_t i = 0; i < 2; i++) {
        if (sticks[i]->isPressed()) {
            anyPressed = true;
            if (holdStartedMs[i] == 0) holdStartedMs[i] = now;
            if (!pauseLatched && now - holdStartedMs[i] >= PAUSE_HOLD_MS) {
                events.pauseToggle = true;
                pauseLatched = true;
                primary.langKlickZaehler = 0;
                secondary.langKlickZaehler = 0;
                primary.reset();
                secondary.reset();
            }
        } else {
            holdStartedMs[i] = 0;
        }
    }
    if (!anyPressed) pauseLatched = false;
    return events;
}

bool GameRuntime::applyPause(UnifiedGameState& state) {
    if (state == GAME_PLAYING) {
        resumeState = state;
        state = GAME_PAUSED;
    } else if (state == GAME_PAUSED) {
        state = resumeState;
    } else {
        return false;
    }
    playSound(SND_UI);
    frameClockStarted = false;
    return true;
}

void GameRuntime::waitForNextFrame(uint16_t frameMs) {
    feedRuntimeWatchdog();
    if (!frameClockStarted) {
        lastFrameWake = xTaskGetTickCount();
        frameClockStarted = true;
    }
    vTaskDelayUntil(&lastFrameWake, pdMS_TO_TICKS(frameMs));
}

void GameRuntime::drawPauseOverlay() const {
    fadeToBlackBy(ledsOben[0], ledsOben.Size(), 175);
    fadeToBlackBy(ledsUnten[0], ledsUnten.Size(), 175);
    CRGB color = themeCol(millis() / 25, 255);
    for (int y = 4; y <= 11; y++) {
        setPixel(13, y, color);
        setPixel(14, y, color);
        setPixel(17, y, color);
        setPixel(18, y, color);
    }
    FastLED.show();
}

void GameRuntime::drawCountdownOverlay(uint8_t number, CRGB color) const {
    drawDigitW(14, 5, constrain(number, 1, 3), color);
}

void GameRuntime::animateTransition(CRGB color) const {
    for (uint8_t frame = 0; frame < 4; frame++) {
        fadeToBlackBy(ledsOben[0], ledsOben.Size(), 70);
        fadeToBlackBy(ledsUnten[0], ledsUnten.Size(), 70);
        uint8_t edge = 70 + frame * 55;
        CRGB pulse = color;
        pulse.nscale8_video(edge);
        for (int x = frame; x < 32 - frame; x++) {
            setPixel(x, frame, pulse);
            setPixel(x, 15 - frame, pulse);
        }
        FastLED.show();
        vTaskDelay(pdMS_TO_TICKS(12));
    }
}

int wrapGameCoordinate(int value, int size) {
    if (size <= 0) return 0;
    if (value < 0) return size - 1;
    if (value >= size) return 0;
    return value;
}

int steppedCappedSpeed(int current, int hits, int everyHits, int step, int maximum) {
    if (everyHits <= 0 || hits <= 0 || hits % everyHits != 0) return min(current, maximum);
    return min(current + step, maximum);
}
