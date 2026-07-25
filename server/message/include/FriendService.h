#pragma once

#include "TcpMessageCodec.h"

namespace wimi {

class ClientForwardService;

class FriendService {
 public:
  explicit FriendService(ClientForwardService &clientForwardService);

  TcpPacket NotifyAddFriend(unsigned int msgID, TcpPacket &request);
  TcpPacket ReplyAddFriend(unsigned int msgID, TcpPacket &request);
  TcpPacket PullFriendApplyList(uint32_t msgID, TcpPacket &request);
  TcpPacket PullFriendList(uint32_t msgID, TcpPacket &request);
  void RemoveFriend(unsigned int msgID, TcpPacket &request);

 private:
  int StoreNotifyAddFriend(TcpPacket &request);
  int StoreReplyAddFriend(TcpPacket &request);

  ClientForwardService &clientForwardService;
};

}  // namespace wimi
