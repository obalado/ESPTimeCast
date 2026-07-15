#pragma once

#include "network_result.h"
#include "nightscout_models.h"

class NightscoutService {
 public:
  NetworkResult fetch(const NightscoutRequest &request, NightscoutData &output);
};
