#include "ConfigStorage.h"

#include <LittleFS.h>

namespace {
SemaphoreHandle_t configMutex = NULL;

void recoverAtomicFile(const char *path) {
    String temporary = String(path) + ".tmp";
    String backup = String(path) + ".bak";
    if (!LittleFS.exists(path) && LittleFS.exists(backup)) {
        LittleFS.rename(backup.c_str(), path);
    } else if (LittleFS.exists(path) && LittleFS.exists(backup)) {
        LittleFS.remove(backup.c_str());
    }
    if (LittleFS.exists(temporary)) LittleFS.remove(temporary.c_str());
}
}

bool lockConfigStorage(TickType_t timeout) {
    return configMutex != NULL &&
           xSemaphoreTakeRecursive(configMutex, timeout) == pdTRUE;
}

void unlockConfigStorage() {
    if (configMutex != NULL) xSemaphoreGiveRecursive(configMutex);
}

bool beginConfigStorage() {
    if (configMutex == NULL)
        configMutex = xSemaphoreCreateRecursiveMutex();
    if (configMutex == NULL) return false;

    ConfigStorageGuard guard;
    if (!guard.ok()) return false;

    if (!LittleFS.begin(false)) {
        // A full chip erase leaves the LittleFS partition blank; an interrupted
        // filesystem upload can leave an invalid directory pair. Recover once
        // here so the rest of the firmware never runs against an unmounted FS.
        Serial.println("LittleFS ungueltig; Partition wird einmalig formatiert.");
        LittleFS.end();
        if (!LittleFS.begin(true)) {
            Serial.println("LittleFS konnte auch nach Formatierung nicht gemountet werden.");
            return false;
        }
        Serial.println("LittleFS wurde wiederhergestellt.");
    }
    recoverAtomicFile("/wifi.json");
    recoverAtomicFile("/device.json");
    recoverAtomicFile("/config.json");
    return true;
}

bool writeJsonAtomically(const char *path, JsonDocument& doc) {
    String temporary = String(path) + ".tmp";
    String backup = String(path) + ".bak";
    if (LittleFS.exists(temporary)) LittleFS.remove(temporary.c_str());

    File file = LittleFS.open(temporary.c_str(), "w");
    if (!file) return false;
    size_t written = serializeJson(doc, file);
    file.flush();
    file.close();
    if (written == 0) {
        LittleFS.remove(temporary.c_str());
        return false;
    }

    if (LittleFS.exists(backup)) LittleFS.remove(backup.c_str());
    bool hadOriginal = LittleFS.exists(path);
    if (hadOriginal && !LittleFS.rename(path, backup.c_str())) {
        LittleFS.remove(temporary.c_str());
        return false;
    }
    if (!LittleFS.rename(temporary.c_str(), path)) {
        if (hadOriginal) LittleFS.rename(backup.c_str(), path);
        return false;
    }
    if (LittleFS.exists(backup)) LittleFS.remove(backup.c_str());
    return true;
}
