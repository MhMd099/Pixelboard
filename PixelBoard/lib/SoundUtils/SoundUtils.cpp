#include "SoundUtils.h"

#include <driver/i2s.h>

#define I2S_BCLK 18
#define I2S_LRC  19
#define I2S_DIN  22
#define SAMPLE_RATE 44100

QueueHandle_t soundQueue = NULL;
volatile bool musicPlaying = false;
volatile int musicBeat = 0;

static volatile uint8_t masterVolume = 100;
static volatile uint8_t effectsVolume = 100;
static volatile uint8_t musicVolume = 100;
static const uint16_t MENU_PCM_CAPACITY = 8192;
static_assert((MENU_PCM_CAPACITY & (MENU_PCM_CAPACITY - 1)) == 0,
              "MENU_PCM_CAPACITY must be a power of two");
static int16_t menuPcm[MENU_PCM_CAPACITY];
// One MP3 decoder produces samples on core 0 and one audio task consumes them
// on core 1. Monotonic atomic indices make this SPSC ring lock-free.
static uint32_t menuPcmRead = 0;
static uint32_t menuPcmWrite = 0;
static bool menuMusicEnabled = false;

struct Note {
  uint16_t freq;
  uint16_t dur;
};

static const Note melody[] = {
  {262,150},{330,150},{392,150},{523,160},{392,150},{330,150},{262,220},{0,90},
  {294,150},{349,150},{440,150},{587,160},{440,150},{349,150},{294,220},{0,90},
  {330,150},{392,150},{494,150},{659,160},{494,150},{392,150},{330,220},{0,90},
  {392,150},{494,150},{587,150},{784,220},{587,150},{494,150},{392,220},{0,260}
};
static const uint8_t melodyLen = sizeof(melody) / sizeof(melody[0]);

struct EffectPattern {
  uint16_t freq[4];
  uint16_t dur[4];
};

static const EffectPattern patterns[] = {
  {{0,0,0,0},          {0,0,0,0}},
  {{850,1450,0,0},     {22,30,0,0}},
  {{1150,1850,0,0},    {35,48,0,0}},
  {{1750,2350,0,0},    {25,35,0,0}},
  {{620,390,190,0},     {55,65,105,0}},
  {{2550,1550,0,0},    {22,30,0,0}},
  {{880,540,0,0},      {34,48,0,0}},
  {{520,310,145,0},     {48,65,105,0}},
  {{180,260,150,0},     {70,55,85,0}},
  {{720,1080,1620,0},   {34,38,52,0}},
  {{1250,0,0,0},       {38,0,0,0}},
  {{720,1120,1680,0},   {28,30,38,0}},
  {{2100,2850,0,0},    {30,48,0,0}},
  {{620,980,1480,2100}, {42,42,45,60}},
  {{1350,1750,2250,0}, {42,42,65,0}},
  {{520,780,1100,1650}, {38,38,42,70}}
};

static const uint8_t EFFECT_VOICES = 5;

struct EffectVoice {
  bool active;
  SoundType type;
  uint8_t step;
  uint8_t priority;
  uint32_t phase;
  int32_t samplesLeft;
  int32_t stepSamples;
  uint32_t startedOrder;
};

static EffectVoice voices[EFFECT_VOICES];
static uint32_t voiceOrder = 0;

static uint8_t soundPriority(SoundType type)
{
  if(type == SND_BOSS) return 5;
  if(type == SND_EXPLOSION || type == SND_DIE) return 4;
  if(type == SND_HIT || type == SND_EAT || type == SND_COIN ||
     type == SND_POWERUP || type == SND_LEVELUP) return 3;
  if(type == SND_SHOOT || type == SND_JUMP || type == SND_CHECKPOINT) return 2;
  if(type == SND_BUFF || type == SND_UI || type == SND_SELECT || type == SND_SWIPE) return 1;
  return 0;
}

static uint16_t perceptualGain(uint8_t volume)
{
  return (uint16_t)(((uint32_t)volume * volume + 50U) / 100U);
}

static int32_t scaledAmplitude(int32_t fullVolumeAmplitude, uint8_t channelVolume)
{
  uint32_t masterGain = perceptualGain(masterVolume);
  uint32_t channelGain = channelVolume;
  return (int32_t)(((int64_t)fullVolumeAmplitude * masterGain * channelGain) / 10000LL);
}

static int32_t envelope(int32_t amplitude, int32_t samplesLeft, int32_t totalSamples,
                        uint16_t attackMs, uint16_t releaseMs)
{
  int32_t elapsed = totalSamples - samplesLeft;
  int32_t attack = max(1, SAMPLE_RATE * (int32_t)attackMs / 1000);
  int32_t release = max(1, SAMPLE_RATE * (int32_t)releaseMs / 1000);
  int32_t level = 32767;
  if(elapsed < attack) level = elapsed * 32767L / attack;
  if(samplesLeft < release) {
    int32_t releaseLevel = (int32_t)(samplesLeft * 32767L / release);
    level = min(level, releaseLevel);
  }
  return amplitude * level / 32767L;
}

static bool initI2S()
{
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = 128,
    .use_apll = false
  };
  i2s_pin_config_t pins = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  esp_err_t result = i2s_driver_install(I2S_NUM_0, &config, 0, NULL);
  if(result != ESP_OK) {
    Serial.printf("I2S-Treiber konnte nicht starten: %d\n", (int)result);
    return false;
  }
  result = i2s_set_pin(I2S_NUM_0, &pins);
  if(result != ESP_OK) {
    Serial.printf("I2S-Pins konnten nicht gesetzt werden: %d\n", (int)result);
    i2s_driver_uninstall(I2S_NUM_0);
    return false;
  }
  result = i2s_zero_dma_buffer(I2S_NUM_0);
  if(result != ESP_OK) {
    Serial.printf("I2S-DMA konnte nicht initialisiert werden: %d\n", (int)result);
    i2s_driver_uninstall(I2S_NUM_0);
    return false;
  }
  return true;
}

static bool loadVoiceStep(EffectVoice& voice)
{
  uint8_t typeIndex = (uint8_t)voice.type;
  if(typeIndex >= sizeof(patterns) / sizeof(patterns[0])) return false;
  while(voice.step < 4 && patterns[typeIndex].dur[voice.step] == 0) voice.step++;
  if(voice.step >= 4) return false;
  voice.stepSamples = SAMPLE_RATE * (int32_t)patterns[typeIndex].dur[voice.step] / 1000;
  voice.samplesLeft = voice.stepSamples;
  voice.phase = 0;
  return true;
}

static void startEffect(SoundType type)
{
  if(type <= SND_NONE || (uint8_t)type >= sizeof(patterns) / sizeof(patterns[0])) return;

  int slot = -1;
  for(uint8_t i = 0; i < EFFECT_VOICES; i++) {
    if(!voices[i].active) { slot = i; break; }
  }
  if(slot < 0) {
    slot = 0;
    for(uint8_t i = 1; i < EFFECT_VOICES; i++) {
      if(voices[i].priority < voices[slot].priority ||
         (voices[i].priority == voices[slot].priority &&
          voices[i].startedOrder < voices[slot].startedOrder)) slot = i;
    }
    if(soundPriority(type) < voices[slot].priority) return;
  }

  EffectVoice& voice = voices[slot];
  voice.active = true;
  voice.type = type;
  voice.step = 0;
  voice.priority = soundPriority(type);
  voice.startedOrder = ++voiceOrder;
  if(!loadVoiceStep(voice)) voice.active = false;
}

static int32_t renderEffectVoice(EffectVoice& voice)
{
  if(!voice.active) return 0;
  if(voice.samplesLeft <= 0) {
    voice.step++;
    if(!loadVoiceStep(voice)) {
      voice.active = false;
      return 0;
    }
  }

  const EffectPattern& pattern = patterns[(uint8_t)voice.type];
  uint16_t frequency = pattern.freq[voice.step];
  voice.phase += frequency;
  if(voice.phase >= SAMPLE_RATE) voice.phase -= SAMPLE_RATE;
  int32_t amplitude = envelope(scaledAmplitude(18500, effectsVolume),
                               voice.samplesLeft, voice.stepSamples, 2, 8);
  voice.samplesLeft--;
  return voice.phase < SAMPLE_RATE / 2 ? amplitude : -amplitude;
}

static uint8_t musicIndex = 0;
static bool musicGap = true;
static uint16_t musicFrequency = 0;
static uint32_t musicPhase = 0;
static int32_t musicSamplesLeft = 0;
static int32_t musicStepSamples = 0;

static void loadNextMusicPart()
{
  if(musicGap) {
    const Note& note = melody[musicIndex];
    musicFrequency = note.freq;
    musicStepSamples = SAMPLE_RATE * (int32_t)note.dur / 1000;
    musicSamplesLeft = musicStepSamples;
    musicGap = false;
    musicPhase = 0;
    musicIndex = (musicIndex + 1) % melodyLen;
    musicBeat++;
  } else {
    musicFrequency = 0;
    musicStepSamples = SAMPLE_RATE / 100;
    musicSamplesLeft = musicStepSamples;
    musicGap = true;
  }
}

static int32_t renderMusicSample()
{
  if(__atomic_load_n(&menuMusicEnabled, __ATOMIC_ACQUIRE)) {
    int16_t sample = 0;
    uint32_t read = __atomic_load_n(&menuPcmRead, __ATOMIC_RELAXED);
    uint32_t write = __atomic_load_n(&menuPcmWrite, __ATOMIC_ACQUIRE);
    if(read != write) {
      sample = menuPcm[read & (MENU_PCM_CAPACITY - 1)];
      __atomic_store_n(&menuPcmRead, read + 1, __ATOMIC_RELEASE);
    }
    uint32_t masterGain = perceptualGain(masterVolume);
    uint32_t channelGain = musicVolume;
    return (int32_t)(((int64_t)sample * masterGain * channelGain) / 10000LL);
  }

  if(!musicPlaying) {
    musicSamplesLeft = 0;
    musicIndex = 0;
    musicGap = true;
    return 0;
  }
  if(musicSamplesLeft <= 0) loadNextMusicPart();
  musicSamplesLeft--;
  if(musicFrequency == 0) return 0;

  musicPhase += musicFrequency;
  if(musicPhase >= SAMPLE_RATE) musicPhase -= SAMPLE_RATE;
  int32_t amplitude = envelope(scaledAmplitude(3000, musicVolume),
                               musicSamplesLeft, musicStepSamples, 4, 10);
  return musicPhase < SAMPLE_RATE / 2 ? amplitude : -amplitude;
}

static void audioTask(void *pvParameters)
{
  if(!initI2S()) {
    vTaskDelete(NULL);
    return;
  }
  static int16_t buffer[128];

  for(;;) {
    SoundType pending;
    while(soundQueue != NULL && xQueueReceive(soundQueue, &pending, 0) == pdTRUE)
      startEffect(pending);

    for(uint16_t i = 0; i < 128; i++) {
      int32_t mixed = renderMusicSample();
      for(uint8_t voice = 0; voice < EFFECT_VOICES; voice++)
        mixed += renderEffectVoice(voices[voice]);
      buffer[i] = (int16_t)constrain(mixed, -32760, 32760);
    }

    size_t written = 0;
    esp_err_t result = i2s_write(I2S_NUM_0, buffer, sizeof(buffer), &written, portMAX_DELAY);
    if(result != ESP_OK || written != sizeof(buffer)) {
      Serial.printf("I2S-Schreibfehler: %d, Bytes: %u\n", (int)result, (unsigned int)written);
      vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
}

void initAudio()
{
  if(soundQueue == NULL) soundQueue = xQueueCreate(16, sizeof(SoundType));
  if(soundQueue == NULL) {
    Serial.println("Audio-Queue konnte nicht erstellt werden.");
    return;
  }
  for(uint8_t i = 0; i < EFFECT_VOICES; i++) voices[i].active = false;
  if(xTaskCreatePinnedToCore(audioTask, "AudioTask", 4096, NULL, 3, NULL, 1) != pdPASS)
    Serial.println("Audio-Task konnte nicht erstellt werden.");
}

void setMasterVolume(int volume) { masterVolume = (uint8_t)constrain(volume, 0, 100); }
uint8_t getMasterVolume() { return masterVolume; }
void setEffectsVolume(int volume) { effectsVolume = (uint8_t)constrain(volume, 0, 100); }
uint8_t getEffectsVolume() { return effectsVolume; }
void setMusicVolume(int volume) { musicVolume = (uint8_t)constrain(volume, 0, 100); }
uint8_t getMusicVolume() { return musicVolume; }

bool pushMenuMusicSample(int16_t sample)
{
  uint32_t write = __atomic_load_n(&menuPcmWrite, __ATOMIC_RELAXED);
  uint32_t read = __atomic_load_n(&menuPcmRead, __ATOMIC_ACQUIRE);
  if(write - read >= MENU_PCM_CAPACITY - 1) return false;
  menuPcm[write & (MENU_PCM_CAPACITY - 1)] = sample;
  __atomic_store_n(&menuPcmWrite, write + 1, __ATOMIC_RELEASE);
  return true;
}

void clearMenuMusicBuffer()
{
  uint32_t write = __atomic_load_n(&menuPcmWrite, __ATOMIC_ACQUIRE);
  __atomic_store_n(&menuPcmRead, write, __ATOMIC_RELEASE);
}

void setMenuMusicEnabled(bool enabled)
{
  __atomic_store_n(&menuMusicEnabled, enabled, __ATOMIC_RELEASE);
}

void playSound(SoundType type)
{
  if(soundQueue == NULL || type == SND_NONE) return;
  if(xQueueSend(soundQueue, &type, 0) != pdTRUE) {
    SoundType discarded;
    xQueueReceive(soundQueue, &discarded, 0);
    xQueueSend(soundQueue, &type, 0);
  }
}

void startMusic() { musicBeat = 0; musicPlaying = true; }
void stopMusic() { musicPlaying = false; }

namespace Audio {
  void playShot() { playSound(SND_SHOOT); }
  void playExplosion() { playSound(SND_EXPLOSION); }
  void playBoss() { playSound(SND_BOSS); }
  void playHit() { playSound(SND_HIT); }
  void playBuff() { playSound(SND_BUFF); }
  void playUI() { playSound(SND_UI); }
  void startMusic() { ::startMusic(); }
  void stopMusic() { ::stopMusic(); }
}
