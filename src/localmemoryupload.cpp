#include "localmemoryupload.h"

#include <cstring>

#include "address.h"
#include "core/iomanager.h"
#include "globalcontext.h"
#include "transfermonitor.h"
#include "ftpconn.h"
#include "localstorage.h"

LocalMemoryUpload::LocalMemoryUpload(LocalStorage* ls) :
  storeid(-1),
  ls(ls)
{
}

void LocalMemoryUpload::engageStore(TransferMonitor* tm, int localtransferid, int storeid, bool ipv6, const std::string& addr, int port, bool ssl, FTPConn* ftpconn) {
  init(tm, localtransferid, ftpconn, Path(), "", ssl, true, port);
  this->storeid = storeid;
  filepos = 0;
  state = LocalTransferState::CONNECTING;
  sockid = interConnect(Address(addr, port, ipv6 ? Core::AddressFamily::IPV6 : Core::AddressFamily::IPV4), ftpconn->getDataProxy());
}

bool LocalMemoryUpload::engageStore(TransferMonitor* tm, int localtransferid, int storeid, bool ipv6, bool ssl, FTPConn* ftpconn) {
  init(tm, localtransferid, ftpconn, Path(), "", ssl, false);
  this->storeid = storeid;
  filepos = 0;
  state = LocalTransferState::LISTENING;
  sockid = interListen(ipv6 ? Core::AddressFamily::IPV6 : Core::AddressFamily::IPV4, ftpconn->getDataProxy());
  return sockid != -1;
}

bool LocalMemoryUpload::beginSource() {
  fileopened = false;
  if (ls->getStoreContent(storeid).empty()) {
    FDFail(sockid, "No in-memory data available to upload");
    return false;
  }
  filepos = 0;
  return true;
}

void LocalMemoryUpload::sendChunk() {
  const Core::BinaryData& data = ls->getStoreContent(storeid);
  if (filepos >= data.size()) {
    global->getIOManager()->closeSocket(sockid);
    tm->localInfo("Upload finished, closing connection");
    tm->sourceComplete();
    return;
  }
  unsigned long long int remaining = data.size() - filepos;
  unsigned int chunk = remaining < buflen ? (unsigned int) remaining : buflen;
  memcpy(buf, data.data() + filepos, chunk);
  filepos += chunk;
  global->getIOManager()->sendData(sockid, buf, chunk);
}
