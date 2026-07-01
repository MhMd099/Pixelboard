#ifndef SOUND_UTILS_H
#define SOUND_UTILS_H

#include <Arduino.h>

enum SoundType {
  SND_NONE,
  SND_SWIPE,
  SND_SELECT,
  SND_EAT,
  SND_DIE,
  SND_SHOOT,
  SND_HIT,
  SND_EXPLOSION,
  SND_BOSS,
  SND_BUFF,
  SND_UI
};

void initAudio();
void playSound(SoundType type);

void startMusic();
void stopMusic();

extern volatile int musicBeat;

namespace Audio {
  void playShot();
  void playExplosion();
  void playBoss();
  void playHit();
  void playBuff();
  void playUI();
  void startMusic();
  void stopMusic();
}

#endif
