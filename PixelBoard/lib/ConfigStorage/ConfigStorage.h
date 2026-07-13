#ifndef CONFIG_STORAGE_H
#define CONFIG_STORAGE_H

#include <Arduino.h>
#include <ArduinoJson.h>

bool lockConfigStorage(TickType_t timeout = portMAX_DELAY);
void unlockConfigStorage();
bool beginConfigStorage();
bool writeJsonAtomically(const char *path, JsonDocument& doc);

class ConfigStorageGuard {
public:
    ConfigStorageGuard() : locked(lockConfigStorage()) {}
    ~ConfigStorageGuard() { if (locked) unlockConfigStorage(); }
    bool ok() const { return locked; }
private:
    bool locked;
};

#endif
