#pragma once

class RequestCallback {
public:
  virtual ~RequestCallback() = default;
  virtual void intermediateData(void* service, int requestid, void* data) {}
  virtual bool requestReady(void* service, int requestid) = 0;
};
