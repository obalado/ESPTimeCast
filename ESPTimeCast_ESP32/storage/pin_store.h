#pragma once

struct PinConfig {
  int clock;
  int chipSelect;
  int data;
};

PinConfig loadPinConfig(const PinConfig &defaults);
