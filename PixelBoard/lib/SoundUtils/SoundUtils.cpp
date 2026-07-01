#include "SoundUtils.h"
#include <driver/i2s.h>

#define I2S_BCLK 18
#define I2S_LRC  19
#define I2S_DIN  22

#define SAMPLE_RATE 44100

QueueHandle_t soundQueue = NULL;

volatile bool musicPlaying = false;
volatile int musicBeat = 0;

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

static void playEffect(int freq,int durationMs)
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

  for(int i=0;i<samples;i++)
  {
    int16_t sample =
      ((i/halfPeriod)&1)
      ?22000
      :-22000;

    i2s_write(
      I2S_NUM_0,
      &sample,
      sizeof(sample),
      &written,
      portMAX_DELAY);
  }

  playSilence();
}

static void playMusicNote(
    int freq,
    int durationMs)
{
  size_t written;

  int samples =
      SAMPLE_RATE * durationMs / 1000;

  if(freq<=0)
  {
    int16_t zero=0;

    for(int i=0;i<samples;i++)
    {
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

  for(int i=0;i<samples;i++)
  {
    int16_t sample =
      ((i/halfPeriod)&1)
      ?2200
      :-2200;

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
    if(musicPlaying)
    {
      const Note n = melodie[musicIndex];

      playMusicNote(n.freq, n.dur);

      playMusicNote(0, 10);

      musicIndex++;

      if(musicIndex >= melodyLen)
        musicIndex = 0;

      musicBeat++;
    }
    else if(soundQueue != NULL &&
            xQueueReceive(
              soundQueue,
              &current,
              pdMS_TO_TICKS(5)))
    {
      musicIndex = 0;

      int freq = 1000;
      int dur  = 30;

      if(current == SND_SWIPE)
      {
        freq = 1200;
        dur  = 30;
      }
      else if(current == SND_SELECT)
      {
        freq = 1500;
        dur  = 45;
      }
      else if(current == SND_EAT)
      {
        freq = 1800;
        dur  = 40;
      }
      else if(current == SND_DIE)
      {
        freq = 400;
        dur  = 90;
      }

      playEffect(freq, dur);
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

void playSound(SoundType type)
{
  if(soundQueue != NULL)
  {
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