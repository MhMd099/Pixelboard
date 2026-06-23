#ifndef TASK_WETTER_H
#define TASK_WETTER_H

#include <Arduino.h>

// Das "extern" sagt dem Compiler: "Suche diese Variable in einer anderen Datei"
extern bool forceWeatherUpdate; 

void taskWetter(void * pvParameters);

#endif