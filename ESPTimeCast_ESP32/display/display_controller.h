#pragma once

#include <Arduino.h>

#include "display_mode.h"

// Owns stable mode state and rotation position. Rendering and availability policy
// remain outside so legacy renderers can migrate incrementally.
class DisplayController {
 public:
  int current = displayModeId(DisplayMode::Clock);
  int previous = -1;
  uint8_t rotationIndex = 0;
  unsigned long switchedAt = 0;

  void syncRotationIndex();
  DisplayMode nextCandidate();
  DisplayMode previousCandidate();
  void select(DisplayMode mode, unsigned long now);
  void force(DisplayMode mode, unsigned long now);
};
