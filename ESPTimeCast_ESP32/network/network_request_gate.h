#pragma once

class NetworkRequestGate {
 public:
  bool tryAcquire();
  void release();
  bool busy() const;

 private:
  volatile bool busy_ = false;
};
