#include "network_request_gate.h"

bool NetworkRequestGate::tryAcquire() {
  if (busy_) return false;
  busy_ = true;
  return true;
}

void NetworkRequestGate::release() {
  busy_ = false;
}

bool NetworkRequestGate::busy() const {
  return busy_;
}
