#ifndef SOUND_UTILS_H
#define SOUND_UTILS_H

#include <Arduino.h>

enum SoundType {
  SND_NONE,
  SND_SWIPE,
  SND_SELECT,
  SND_EAT,
  SND_DIE
};

void initAudio();
void playSound(SoundType type);

void startMusic();
void stopMusic();

extern volatile int musicBeat;

#endif