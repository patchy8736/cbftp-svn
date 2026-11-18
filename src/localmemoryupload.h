#pragma once

#include "localupload.h"

class LocalStorage;

// Upload pump sourcing bytes from an in-memory store (LocalStorage storeid)
// instead of a file on disk. Used by in-memory transfers.
class LocalMemoryUpload : public LocalUpload {
public:
  LocalMemoryUpload(LocalStorage* ls);
  void engageStore(TransferMonitor* tm, int localtransferid, int storeid, bool ipv6, const std::string& addr, int port, bool ssl, FTPConn* ftpconn);
  bool engageStore(TransferMonitor* tm, int localtransferid, int storeid, bool ipv6, bool ssl, FTPConn* ftpconn);
private:
  bool beginSource() override;
  void sendChunk() override;
  int storeid;
  LocalStorage * ls;
};
