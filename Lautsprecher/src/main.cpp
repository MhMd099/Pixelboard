#include <Arduino.h>
#include <driver/i2s.h>

// Pins für I2S
#define I2S_BCLK 26
#define I2S_LRC  25
#define I2S_DIN  22
#define BTN_PIN  14

// Audio-Einstellungen
#define SAMPLE_RATE 16000
#define BUFFER_SIZE 64

bool isPlaying = false;
static bool lastBtnState = HIGH; // Da INPUT_PULLUP, ist "Nicht gedrückt" HIGH

void setupI2S() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = BUFFER_SIZE
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BCLK,
        .ws_io_num = I2S_LRC,
        .data_out_num = I2S_DIN,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pin_config);
}

void audioTask(void *pvParameters) {
    setupI2S();
    
    // Sinuswelle-Puffer generieren (für einen Ton)
    int16_t sineWave[BUFFER_SIZE];
    for(int i=0; i<BUFFER_SIZE; i++) {
        sineWave[i] = (int16_t)(sin(i * 0.1) * 10000); 
    }

    pinMode(BTN_PIN, INPUT_PULLUP);

    for(;;) {
        // Button Abfrage (Entprellt)
        bool currentBtn = digitalRead(BTN_PIN);
        if (currentBtn == LOW && lastBtnState == HIGH) {
            isPlaying = !isPlaying; // Toggle
            Serial.println(isPlaying ? "Audio AN" : "Audio AUS");
            vTaskDelay(200 / portTICK_PERIOD_MS); // Entprellung
        }
        lastBtnState = currentBtn;

        // Audio-Ausgabe
        size_t bytesWritten;
        if (isPlaying) {
            // Ton abspielen
            i2s_write(I2S_NUM_0, sineWave, sizeof(sineWave), &bytesWritten, portMAX_DELAY);
        } else {
            // Stille senden
            uint8_t zeroBuffer[BUFFER_SIZE] = {0};
            i2s_write(I2S_NUM_0, zeroBuffer, sizeof(zeroBuffer), &bytesWritten, portMAX_DELAY);
        }
        
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void setup() {
    Serial.begin(115200);
    xTaskCreate(audioTask, "AudioTask", 2048, NULL, 1, NULL);
}

void loop() {
    vTaskDelete(NULL); // Wir brauchen den loop nicht, alles passiert im Task
}