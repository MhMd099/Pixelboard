#include <Arduino.h>
#include <driver/i2s.h>

// Pins für MAX98357A (Da 25 und 26 besetzt sind)
#define I2S_BCLK 18
#define I2S_LRC  19
#define I2S_DIN  22

// Button Pin
#define BTN_PIN  14

// I2S Konfiguration
#define SAMPLE_RATE 44100
#define VOLUME 1000 // Lautstärke (0 bis 1000+)

bool playing = false;

void initI2S() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 64,
        .use_apll = false
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BCLK,
        .ws_io_num = I2S_LRC,
        .data_out_num = I2S_DIN,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
    i2s_set_clk(I2S_NUM_0, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
}

// Erzeugt einen einfachen Sinus-Ton-Buffer
void playTone() {
    size_t bytes_written;
    int16_t sample = 0;
    static float phase = 0;
    
    // Kleiner Buffer mit einer Sinuswelle
    int16_t buffer[64];
    for(int i = 0; i < 64; i++) {
        buffer[i] = (int16_t)(sin(phase) * VOLUME);
        phase += 0.1; // Frequenz anpassen
        if (phase > 2 * PI) phase -= 2 * PI;
    }
    i2s_write(I2S_NUM_0, buffer, sizeof(buffer), &bytes_written, portMAX_DELAY);
}

void playSilence() {
    size_t bytes_written;
    int16_t buffer[64] = {0}; // Stille
    i2s_write(I2S_NUM_0, buffer, sizeof(buffer), &bytes_written, portMAX_DELAY);
}

void audioTask(void *pvParameters) {
    initI2S();
    
    bool lastBtnState = HIGH;
    
    for(;;) {
        // Debounce Logic
        bool currentBtnState = digitalRead(BTN_PIN);
        if (currentBtnState == LOW && lastBtnState == HIGH) {
            playing = !playing; // Toggle
            Serial.println(playing ? "Ton AN" : "Ton AUS");
            vTaskDelay(200 / portTICK_PERIOD_MS); // Kleine Entprellung
        }
        lastBtnState = currentBtnState;

        if (playing) {
            playTone();
        } else {
            playSilence();
        }
        
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(BTN_PIN, INPUT_PULLUP); // Pin 14 ist internen Pullup
    
    // Task erstellen
    xTaskCreate(audioTask, "AudioTask", 2048, NULL, 1, NULL);
}

void loop() {
    // Leer, da alles im Task läuft
    vTaskDelete(NULL); 
}