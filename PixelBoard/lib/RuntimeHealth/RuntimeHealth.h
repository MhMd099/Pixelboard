#ifndef RUNTIME_HEALTH_H
#define RUNTIME_HEALTH_H

#include <stdint.h>

void initRuntimeWatchdog();
void registerCurrentTaskWithWatchdog();
void feedRuntimeWatchdog();
void delayWithRuntimeWatchdog(uint32_t milliseconds);

#endif
