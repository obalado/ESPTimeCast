#include "display_controller.h"

void DisplayController::syncRotationIndex() {
  for (size_t index = 0; index < kDisplayModeOrderCount; ++index) {
    if (displayModeId(kDisplayModeOrder[index]) == current) {
      rotationIndex = static_cast<uint8_t>(index);
      return;
    }
  }
}

DisplayMode DisplayController::nextCandidate() {
  rotationIndex = static_cast<uint8_t>((rotationIndex + 1) % kDisplayModeOrderCount);
  return kDisplayModeOrder[rotationIndex];
}

DisplayMode DisplayController::previousCandidate() {
  if (rotationIndex == 0) rotationIndex = static_cast<uint8_t>(kDisplayModeOrderCount - 1);
  else --rotationIndex;
  return kDisplayModeOrder[rotationIndex];
}

void DisplayController::select(DisplayMode mode, unsigned long now) {
  previous = current;
  current = displayModeId(mode);
  switchedAt = now;
  syncRotationIndex();
}

void DisplayController::force(DisplayMode mode, unsigned long now) {
  current = displayModeId(mode);
  switchedAt = now;
  syncRotationIndex();
}
