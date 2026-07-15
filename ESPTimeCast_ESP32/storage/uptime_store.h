#pragma once

#include <Arduino.h>

void loadUptime();
void resetUptimeSession();
void saveUptime();
unsigned long getTotalRuntimeSeconds();
String formatTotalRuntime();
String formatUptime(unsigned long seconds);
