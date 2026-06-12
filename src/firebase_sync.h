#pragma once

#include "system_state.h"

namespace FirebaseSync {

void begin();
void loop(const SensorSnapshot& sensors, bool pumpOn, const char* pumpReason);
void publishStateNow(bool pumpOn, const char* pumpReason);
bool ready();
bool manualMode();
bool manualPumpRequested();
const char* controlModeName();

}  // namespace FirebaseSync
