#include "SoundUtils.h"
#include <driver/i2s.h>

#define I2S_BCLK 18
#define I2S_LRC  19
#define I2S_DIN  22

#define SAMPLE_RATE 44100

QueueHandle_t soundQueue = NULL;

volatile bool musicPlaying = false;
volatile int musicBeat = 0;
static volatile uint32_t soundSerial = 0;
static volatile uint8_t activeSoundPriority = 0;
static volatile uint8_t masterVolume = 100;
static volatile uint8_t effectsVolume = 100;
static volatile uint8_t musicVolume = 100;

struct Note {
  uint16_t freq;
  uint16_t dur;
};

static const Note melodie[] = {
  {262,150},{330,150},{392,150},{523,160},
  {392,150},{330,150},{262,220},{0,90},

  {294,150},{349,150},{440,150},{587,160},
  {440,150},{349,150},{294,220},{0,90},

  {330,150},{392,150},{494,150},{659,160},
  {494,150},{392,150},{330,220},{0,90},

  {392,150},{494,150},{587,150},{784,220},
  {587,150},{494,150},{392,220},{0,260},
};

static const int melodyLen =
    sizeof(melodie) / sizeof(melodie[0]);

static uint8_t soundPriority(SoundType type)
{
  if(type == SND_BOSS) return 5;
  if(type == SND_EXPLOSION || type == SND_DIE) return 4;
  if(type == SND_HIT || type == SND_EAT || type == SND_COIN || type == SND_POWERUP || type == SND_LEVELUP) return 3;
  if(type == SND_SHOOT || type == SND_JUMP || type == SND_CHECKPOINT) return 2;
  if(type == SND_BUFF || type == SND_UI || type == SND_SELECT || type == SND_SWIPE) return 1;
  return 0;
}

static void initI2S()
{
  i2s_config_t i2sConfig = {
    .mode = (i2s_mode_t)
      (I2S_MODE_MASTER | I2S_MODE_TX),

    .sample_rate = SAMPLE_RATE,

    .bits_per_sample =
      I2S_BITS_PER_SAMPLE_16BIT,

    .channel_format =
      I2S_CHANNEL_FMT_ONLY_LEFT,

    .communication_format =
      I2S_COMM_FORMAT_STAND_I2S,

    .intr_alloc_flags =
      ESP_INTR_FLAG_LEVEL1,

    .dma_buf_count = 2,
    .dma_buf_len = 32,

    .use_apll = false
  };

  i2s_pin_config_t pinConfig = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(
      I2S_NUM_0,
      &i2sConfig,
      0,
      NULL);

  i2s_set_pin(
      I2S_NUM_0,
      &pinConfig);
}

static void playSilence()
{
  size_t written;

  int16_t zero[2] = {0,0};

  i2s_zero_dma_buffer(I2S_NUM_0);

  i2s_write(
      I2S_NUM_0,
      zero,
      sizeof(zero),
      &written,
      portMAX_DELAY);
}

static bool interrupted(uint32_t localSerial)
{
  return soundSerial != localSerial;
}

static uint16_t perceptualGain(uint8_t volume)
{
  return (uint16_t)(((uint32_t)volume * volume + 50U) / 100U);
}

static int16_t scaledAmplitude(int16_t fullVolumeAmplitude, uint8_t channelVolume)
{
  uint32_t masterGain = perceptualGain(masterVolume);
  uint32_t channelGain = perceptualGain(channelVolume);
  return (int16_t)(((int64_t)fullVolumeAmplitude * masterGain * channelGain) / 10000LL);
}

static int16_t applyEnvelope(int16_t amplitude, int sampleIndex, int totalSamples,
                             int attackMs, int releaseMs)
{
  int attackSamples = max(1, SAMPLE_RATE * attackMs / 1000);
  int releaseSamples = max(1, SAMPLE_RATE * releaseMs / 1000);
  int32_t level = 32767;
  if(sampleIndex < attackSamples)
    level = (int32_t)sampleIndex * 32767 / attackSamples;

  int remaining = totalSamples - sampleIndex - 1;
  if(remaining < releaseSamples)
  {
    int32_t releaseLevel = max(0, remaining) * 32767L / releaseSamples;
    if(releaseLevel < level) level = releaseLevel;
  }
  return (int16_t)(((int32_t)amplitude * level) / 32767L);
}

static void playEffect(int freq,int durationMs,uint32_t localSerial)
{
  size_t written;

  i2s_zero_dma_buffer(I2S_NUM_0);

  int samples =
      SAMPLE_RATE * durationMs / 1000;

  if(freq<=0)
  {
    playSilence();
    return;
  }

  int halfPeriod =
      SAMPLE_RATE / freq / 2;

  if(halfPeriod<1)
    halfPeriod=1;

  // Effects need enough headroom to remain audible on small matrix speakers.
  int16_t baseAmplitude = scaledAmplitude(29000, effectsVolume);

  for(int i=0;i<samples;i++)
  {
    if((i & 0x3F) == 0)
    {
      if(interrupted(localSerial))
        break;
      baseAmplitude = scaledAmplitude(29000, effectsVolume);
    }

    int16_t amplitude = applyEnvelope(baseAmplitude, i, samples, 2, 7);

    int16_t sample =
      ((i/halfPeriod)&1)
      ?amplitude
      :-amplitude;

    i2s_write(
      I2S_NUM_0,
      &sample,
      sizeof(sample),
      &written,
      portMAX_DELAY);
  }

  playSilence();
}

static void playEffectPattern(SoundType type, uint32_t localSerial)
{
  auto tone = [&](int frequency, int durationMs) {
    if(!interrupted(localSerial))
      playEffect(frequency, durationMs, localSerial);
  };

  switch(type)
  {
    case SND_SWIPE:      tone(850, 22);  tone(1450, 30); break;
    case SND_SELECT:     tone(1150, 35); tone(1850, 48); break;
    case SND_EAT:        tone(1750, 25); tone(2350, 35); break;
    case SND_DIE:        tone(620, 55);  tone(390, 65); tone(190, 105); break;
    case SND_SHOOT:      tone(2550, 22); tone(1550, 30); break;
    case SND_HIT:        tone(880, 34);  tone(540, 48); break;
    case SND_EXPLOSION:  tone(520, 48);  tone(310, 65); tone(145, 105); break;
    case SND_BOSS:       tone(180, 70);  tone(260, 55); tone(150, 85); break;
    case SND_BUFF:       tone(720, 34);  tone(1080, 38); tone(1620, 52); break;
    case SND_UI:         tone(1250, 38); break;
    case SND_JUMP:       tone(720, 28);  tone(1120, 30); tone(1680, 38); break;
    case SND_COIN:       tone(2100, 30); tone(2850, 48); break;
    case SND_POWERUP:    tone(620, 42);  tone(980, 42); tone(1480, 45); tone(2100, 60); break;
    case SND_CHECKPOINT: tone(1350, 42); tone(1750, 42); tone(2250, 65); break;
    case SND_LEVELUP:    tone(520, 38);  tone(780, 38); tone(1100, 42); tone(1650, 70); break;
    default:             tone(1000, 45); break;
  }
}

static void playMusicNote(
    int freq,
    int durationMs,
    uint32_t localSerial)
{
  size_t written;

  int samples =
      SAMPLE_RATE * durationMs / 1000;

  if(freq<=0)
  {
    int16_t zero=0;

    for(int i=0;i<samples;i++)
    {
      if((i & 0x3F) == 0 && interrupted(localSerial))
        return;

      i2s_write(
          I2S_NUM_0,
          &zero,
          sizeof(zero),
          &written,
          portMAX_DELAY);
    }

    return;
  }

  int halfPeriod =
      SAMPLE_RATE / freq / 2;

  if(halfPeriod<1)
    halfPeriod=1;

  int16_t baseAmplitude = scaledAmplitude(2200, musicVolume);

  for(int i=0;i<samples;i++)
  {
    if((i & 0x3F) == 0)
    {
      if(interrupted(localSerial))
        return;
      baseAmplitude = scaledAmplitude(2200, musicVolume);
    }

    int16_t amplitude = applyEnvelope(baseAmplitude, i, samples, 4, 10);

    int16_t sample =
      ((i/halfPeriod)&1)
      ?amplitude
      :-amplitude;

    i2s_write(
      I2S_NUM_0,
      &sample,
      sizeof(sample),
      &written,
      portMAX_DELAY);
  }
}
static void audioTask(void *pvParameters)
{
  initI2S();

  SoundType current;

  int musicIndex = 0;

  for(;;)
  {
    if(soundQueue != NULL &&
       xQueueReceive(soundQueue, &current, musicPlaying ? 0 : pdMS_TO_TICKS(5)))
    {
      musicIndex = 0;

      activeSoundPriority = soundPriority(current);
      uint32_t localSerial = soundSerial;
      playEffectPattern(current, localSerial);
      activeSoundPriority = 0;
    }
    else if(musicPlaying)
    {
      const Note n = melodie[musicIndex];
      uint32_t localSerial = soundSerial;

      playMusicNote(n.freq, n.dur, localSerial);

      playMusicNote(0, 10, localSerial);

      musicIndex++;

      if(musicIndex >= melodyLen)
        musicIndex = 0;

      musicBeat++;
    }
    else
    {
      playSilence();
    }

    vTaskDelay(1);
  }
}

void initAudio()
{
  if(soundQueue == NULL)
    soundQueue =
      xQueueCreate(10,sizeof(SoundType));

  xTaskCreatePinnedToCore(
      audioTask,
      "AudioTask",
      4096,
      NULL,
      2,
      NULL,
      1);
}

void setMasterVolume(int volume)
{
  masterVolume = (uint8_t)constrain(volume, 0, 100);
}

uint8_t getMasterVolume()
{
  return masterVolume;
}

void setEffectsVolume(int volume)
{
  effectsVolume = (uint8_t)constrain(volume, 0, 100);
}

uint8_t getEffectsVolume()
{
  return effectsVolume;
}

void setMusicVolume(int volume)
{
  musicVolume = (uint8_t)constrain(volume, 0, 100);
}

uint8_t getMusicVolume()
{
  return musicVolume;
}

void playSound(SoundType type)
{
  if(soundQueue != NULL)
  {
    uint8_t priority = soundPriority(type);
    if(activeSoundPriority > 0 && priority < activeSoundPriority)
      return;

    soundSerial++;
    xQueueReset(soundQueue);

    xQueueSend(soundQueue,&type,0);
  }
}

void startMusic()
{
  musicBeat = 0;
  musicPlaying = true;
}

void stopMusic()
{
  musicPlaying = false;
}

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
