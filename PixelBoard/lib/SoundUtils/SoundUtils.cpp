#include "SoundUtils.h"
#include <driver/i2s.h>

#define I2S_BCLK 18
#define I2S_LRC  19
#define I2S_DIN  22

QueueHandle_t soundQueue = NULL;
volatile bool musicPlaying = false;
volatile int  musicBeat = 0;

// --- Kleine prozedurale Melodie (loopt). Frei änderbar, kostet fast keinen Flash. ---
struct Note { uint16_t freq; uint16_t dur; };
static const Note melodie[] = {
    {262, 150}, {330, 150}, {392, 150}, {523, 160}, // C  E  G  C5
    {392, 150}, {330, 150}, {262, 220}, {0, 90},    // G  E  C
    {294, 150}, {349, 150}, {440, 150}, {587, 160}, // D  F  A  D5
    {440, 150}, {349, 150}, {294, 220}, {0, 90},    // A  F  D
    {330, 150}, {392, 150}, {494, 150}, {659, 160}, // E  G  B  E5
    {494, 150}, {392, 150}, {330, 220}, {0, 90},    // B  G  E
    {392, 150}, {494, 150}, {587, 150}, {784, 220}, // G  B  D5 G5
    {587, 150}, {494, 150}, {392, 220}, {0, 260},   // D5 B  G
};
static const int MELODIE_LEN = sizeof(melodie) / sizeof(melodie[0]);

// Schreibt einen Rechteck-Ton (freq<=0 = Stille) der angegebenen Dauer aufs I2S.
static void writeTon(int freq, int durationMs) {
    size_t bytes_written;
    int total = (44100 * durationMs) / 1000;
    if (freq <= 0) {
        int16_t s = 0;
        for (int i = 0; i < total; i++) i2s_write(I2S_NUM_0, &s, 2, &bytes_written, portMAX_DELAY);
        return;
    }
    int halfPeriod = 44100 / freq / 2;
    if (halfPeriod < 1) halfPeriod = 1;
    for (int i = 0; i < total; i++) {
        int16_t sample = ((i / halfPeriod) % 2) ? 4000 : -4000;
        i2s_write(I2S_NUM_0, &sample, 2, &bytes_written, portMAX_DELAY);
    }
}

void audioTask(void *pvParameters) {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 44100,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = 64,
        .use_apll = false
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BCLK, .ws_io_num = I2S_LRC, .data_out_num = I2S_DIN, .data_in_num = I2S_PIN_NO_CHANGE
    };

    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);

    SoundType currentSnd;
    size_t bytes_written;
    int melIdx = 0;

    for (;;) {
        if (musicPlaying) {
            // Melodie Note für Note abspielen und loopen
            const Note n = melodie[melIdx];
            writeTon(n.freq, n.dur);
            writeTon(0, 12); // kurze Lücke, damit Noten nicht verschmelzen
            melIdx = (melIdx + 1) % MELODIE_LEN;
            musicBeat++;
        }
        else if (soundQueue != NULL && xQueueReceive(soundQueue, &currentSnd, pdMS_TO_TICKS(10))) {
            melIdx = 0; // Melodie startet beim nächsten Mal von vorne
            int freq = 1000;
            int duration = 25;
            // Kurze, knackige Klicks statt langer Töne
            if (currentSnd == SND_SWIPE)       { freq = 1200; duration = 15; }
            else if (currentSnd == SND_SELECT) { freq = 1500; duration = 25; }
            else if (currentSnd == SND_EAT)    { freq = 1800; duration = 18; }
            else if (currentSnd == SND_DIE)    { freq = 400;  duration = 120; }
            writeTon(freq, duration);
        }
        else {
            int16_t zero = 0;
            i2s_write(I2S_NUM_0, &zero, 2, &bytes_written, portMAX_DELAY);
        }
        vTaskDelay(1);
    }
}

void initAudio() {
    if (soundQueue == NULL) soundQueue = xQueueCreate(10, sizeof(SoundType));
    xTaskCreatePinnedToCore(audioTask, "AudioTask", 2560, NULL, 2, NULL, 1);
}

void playSound(SoundType type) {
    if (soundQueue != NULL) {
        xQueueSend(soundQueue, &type, 0);
    }
}

void startMusic() { musicBeat = 0; musicPlaying = true; }
void stopMusic()  { musicPlaying = false; }
