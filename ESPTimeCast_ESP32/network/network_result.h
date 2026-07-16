#pragma once

enum class NetworkError {
  None,
  Disconnected,
  InvalidConfig,
  HttpError,
  ParseError
};

struct NetworkResult {
  NetworkError error = NetworkError::None;
  int httpStatus = 0;

  bool ok() const { return error == NetworkError::None; }
};
