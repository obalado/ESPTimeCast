#include "pin_store.h"

#include <Arduino.h>
#include <Preferences.h>

PinConfig loadPinConfig(const PinConfig &defaults) {
  Preferences preferences;
  preferences.begin("pins", false);

  const bool hasClock = preferences.isKey("clk");
  const bool hasChipSelect = preferences.isKey("cs");
  const bool hasData = preferences.isKey("data");

  if (!(hasClock && hasChipSelect && hasData)) {
    Serial.println("[PIN CONFIG] Missing NVS keys - MIGRATION TRIGGERED");
    if (!hasClock) preferences.putInt("clk", defaults.clock);
    if (!hasChipSelect) preferences.putInt("cs", defaults.chipSelect);
    if (!hasData) preferences.putInt("data", defaults.data);
    Serial.println("[PIN CONFIG] Migration complete (non-destructive)");
  }

  PinConfig result{
    preferences.getInt("clk", defaults.clock),
    preferences.getInt("cs", defaults.chipSelect),
    preferences.getInt("data", defaults.data),
  };
  preferences.end();

  if (result.clock < 0 || result.chipSelect < 0 || result.data < 0) {
    Serial.println("[PIN CONFIG] Invalid pins - fallback to defaults");
    result = defaults;
  }

  Serial.printf("[PIN CONFIG] Loaded pins - CLK:%d CS:%d DATA:%d\n",
                result.clock, result.chipSelect, result.data);
  return result;
}
