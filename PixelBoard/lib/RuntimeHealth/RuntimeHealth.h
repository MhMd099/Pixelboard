#ifndef RUNTIME_HEALTH_H
#define RUNTIME_HEALTH_H

void initRuntimeWatchdog();
void registerCurrentTaskWithWatchdog();
void feedRuntimeWatchdog();

#endif
