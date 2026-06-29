This file is a merged representation of the entire codebase, combined into a single document by Repomix.

# File Summary

## Purpose
This file contains a packed representation of the entire repository's contents.
It is designed to be easily consumable by AI systems for analysis, code review,
or other automated processes.

## File Format
The content is organized as follows:
1. This summary section
2. Repository information
3. Directory structure
4. Repository files (if enabled)
5. Multiple file entries, each consisting of:
  a. A header with the file path (## File: path/to/file)
  b. The full contents of the file in a code block

## Usage Guidelines
- This file should be treated as read-only. Any changes should be made to the
  original repository files, not this packed version.
- When processing this file, use the file path to distinguish
  between different files in the repository.
- Be aware that this file may contain sensitive information. Handle it with
  the same level of security as you would the original repository.

## Notes
- Some files may have been excluded based on .gitignore rules and Repomix's configuration
- Binary files are not included in this packed representation. Please refer to the Repository Structure section for a complete list of file paths, including binary files
- Files matching patterns in .gitignore are excluded
- Files matching default ignore patterns are excluded
- Files are sorted by Git change count (files with more changes are at the bottom)

# Directory Structure
```
.gitignore
include/README
lib/README
platformio.ini
src/main.cpp
test/README
```

# Files

## File: .gitignore
````
.pio
.vscode/.browse.c_cpp.db*
.vscode/c_cpp_properties.json
.vscode/launch.json
.vscode/ipch
````

## File: include/README
````
This directory is intended for project header files.

A header file is a file containing C declarations and macro definitions
to be shared between several project source files. You request the use of a
header file in your project source file (C, C++, etc) located in `src` folder
by including it, with the C preprocessing directive `#include'.

```src/main.c

#include "header.h"

int main (void)
{
 ...
}
```

Including a header file produces the same results as copying the header file
into each source file that needs it. Such copying would be time-consuming
and error-prone. With a header file, the related declarations appear
in only one place. If they need to be changed, they can be changed in one
place, and programs that include the header file will automatically use the
new version when next recompiled. The header file eliminates the labor of
finding and changing all the copies as well as the risk that a failure to
find one copy will result in inconsistencies within a program.

In C, the convention is to give header files names that end with `.h'.

Read more about using header files in official GCC documentation:

* Include Syntax
* Include Operation
* Once-Only Headers
* Computed Includes

https://gcc.gnu.org/onlinedocs/cpp/Header-Files.html
````

## File: lib/README
````
This directory is intended for project specific (private) libraries.
PlatformIO will compile them to static libraries and link into the executable file.

The source code of each library should be placed in a separate directory
("lib/your_library_name/[Code]").

For example, see the structure of the following example libraries `Foo` and `Bar`:

|--lib
|  |
|  |--Bar
|  |  |--docs
|  |  |--examples
|  |  |--src
|  |     |- Bar.c
|  |     |- Bar.h
|  |  |- library.json (optional. for custom build options, etc) https://docs.platformio.org/page/librarymanager/config.html
|  |
|  |--Foo
|  |  |- Foo.c
|  |  |- Foo.h
|  |
|  |- README --> THIS FILE
|
|- platformio.ini
|--src
   |- main.c

Example contents of `src/main.c` using Foo and Bar:
```
#include <Foo.h>
#include <Bar.h>

int main (void)
{
  ...
}

```

The PlatformIO Library Dependency Finder will find automatically dependent
libraries by scanning project source files.

More information about PlatformIO Library Dependency Finder
- https://docs.platformio.org/page/librarymanager/ldf.html
````

## File: platformio.ini
````ini
; PlatformIO Project Configuration File
;
;   Build options: build flags, source filter
;   Upload options: custom upload port, speed and extra flags
;   Library options: dependencies, extra library storages
;   Advanced options: extra scripting
;
; Please visit documentation for the other options and examples
; https://docs.platformio.org/page/projectconf.html

[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
````

## File: src/main.cpp
````cpp
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
#define VOLUME 20000 // Lautstärke (0 bis 1000+)

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
````

## File: test/README
````
This directory is intended for PlatformIO Test Runner and project tests.

Unit Testing is a software testing method by which individual units of
source code, sets of one or more MCU program modules together with associated
control data, usage procedures, and operating procedures, are tested to
determine whether they are fit for use. Unit testing finds problems early
in the development cycle.

More information about PlatformIO Unit Testing:
- https://docs.platformio.org/en/latest/advanced/unit-testing/index.html
````
