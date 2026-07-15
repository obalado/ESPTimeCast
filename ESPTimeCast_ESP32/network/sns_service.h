#pragma once

#include "network_result.h"
#include "sns_models.h"

class SnsService {
 public:
  NetworkResult fetch(const SnsRequest &request, SnsData &output);

 private:
  NetworkResult fetchYouTube(const String &source, SnsData &output);
  NetworkResult fetchInstagram(const String &source, SnsData &output);
  NetworkResult fetchRss(const String &source, SnsData &output);
};
