#ifndef APP_NAVIGATION_H
#define APP_NAVIGATION_H

// Thread-safe request used by the web task. The main loop performs the actual
// transition so display, input and audio always follow the same path.
bool requestAppTaskSwitch(int taskNumber);

#endif
