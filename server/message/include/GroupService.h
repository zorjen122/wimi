#pragma once

#include "TcpMessageCodec.h"

#include <cstdint>
#include <string>

namespace wimi {

class ClientForwardService;

class GroupService {
 public:
  explicit GroupService(ClientForwardService &clientForwardService);

  TcpPacket Create(unsigned int msgID, TcpPacket &request);
  TcpPacket NotifyJoin(unsigned int msgID, TcpPacket &request);
  TcpPacket PullNotify(unsigned int msgID, TcpPacket &request);
  TcpPacket ReplyJoin(unsigned int msgID, TcpPacket &request);
  TcpPacket Quit(unsigned int msgID, TcpPacket &request);

 private:
  int NotifyMemberJoin(int64_t uid, int64_t groupId,
                       const std::string &requestMessage);
  int NotifyMemberReply(int64_t groupId, int64_t managerUid,
                        int64_t requestorUid, bool accept);

  ClientForwardService &clientForwardService;
};

}  // namespace wimi
