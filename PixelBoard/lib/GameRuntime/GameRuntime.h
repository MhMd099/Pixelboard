#ifndef GAME_RUNTIME_H
#define GAME_RUNTIME_H

#include <Arduino.h>
#include <FastLED.h>

class Joystick;

enum UnifiedGameState : uint8_t {
    GAME_MENU,
    GAME_COUNTDOWN,
    GAME_PLAYING,
    GAME_PAUSED,
    GAME_GAME_OVER
};

struct GameStickState {
    int x;
    int y;
    bool pressed;
    bool simpleClick;
    bool doubleClick;
    bool longClick;
};

struct GameControlEvents {
    GameStickState primary;
    GameStickState secondary;
    bool doubleClick;
    bool primaryDoubleClick;
    bool secondaryDoubleClick;
    bool pauseToggle;
};

class GameRuntime {
public:
    GameRuntime();
    void begin();
    GameControlEvents poll(Joystick& primary, Joystick& secondary);
    bool applyPause(UnifiedGameState& state);
    void waitForNextFrame(uint16_t frameMs = 16);
    void drawPauseOverlay() const;
    void drawCountdownOverlay(uint8_t number, CRGB color = CRGB::Yellow) const;
    void animateTransition(CRGB color) const;

private:
    uint32_t holdStartedMs[2];
    bool pauseLatched;
    bool frameClockStarted;
    TickType_t lastFrameWake;
    UnifiedGameState resumeState;
};

int wrapGameCoordinate(int value, int size);
int steppedCappedSpeed(int current, int hits, int everyHits, int step, int maximum);

#endif
