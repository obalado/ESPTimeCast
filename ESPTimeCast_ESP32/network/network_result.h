#pragma once

enum class NetworkError {
  None,
  Busy,
  Disconnected,
  InvalidConfig,
  LowMemory,
  HttpError,
  ParseError,
  NotFound,
  RateLimited
};

struct NetworkResult {
  NetworkError error = NetworkError::None;
  int httpStatus = 0;

  bool ok() const { return error == NetworkError::None; }
};
