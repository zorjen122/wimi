#include "FriendService.h"
#include "DbGlobal.h"
#include "ClientForwardService.h"

#include "Const.h"
#include "Logger.h"
#include "Mysql.h"
#include "Redis.h"

#include <spdlog/spdlog.h>
#include <string>
namespace wimi {

FriendService::FriendService(ClientForwardService &clientForwardService)
    : clientForwardService(clientForwardService) {}

int FriendService::StoreNotifyAddFriend(TcpPacket &request) {
  long from = request.uid();
  long to = request.to();
  std::string requestMessage = request.request_message();
  db::FriendApply::Ptr friendApply(new db::FriendApply(
      from, to, db::FriendApply::Status::Wait, requestMessage));

  int ret = db::MysqlDao::GetInstance()->insertFriendApply(friendApply);
  return ret;
}

TcpPacket FriendService::NotifyAddFriend(unsigned int msgID,
                                         TcpPacket &request) {
  TcpPacket rsp;

  long from = request.uid();
  long to = request.to();
  // actor 只取 canonical uid，from 仅作为接收端兼容展示字段。
  request.set_from(from);

  int ret = StoreNotifyAddFriend(request);
  rsp.set_uid(to);
  if (ret == -1) {
    LOG_INFO(businessLogger,
             "StoreageNotifyAddFriend save service failed, from-{}, to-{}",
             from, to);
    rsp.set_error(ErrorCodes::MysqlFailed);
    rsp.set_message("数据库修改异常");
    return rsp;
  }

  TcpPacket senderRsp = request;
  senderRsp.clear_skip_storage();
  senderRsp.set_seq(db::RedisDao::GetInstance()->generateMsgId());
  if (clientForwardService.ForwardToGateway(
          to, SerializeTcpPacket(senderRsp), ID_NOTIFY_ADD_FRIEND_REQ)) {
    rsp.set_error(ErrorCodes::Success);
    return rsp;
  }

  LOG_DEBUG(businessLogger,
            "friend apply stored but target is not on a healthy gateway, from: "
            "{}, to: {}",
            from, to);
  rsp.set_error(ErrorCodes::Success);
  return rsp;
}

TcpPacket FriendService::ReplyAddFriend(unsigned int msgID,
                                        TcpPacket &request) {
  TcpPacket rsp;

  long from = request.uid();
  long to = request.to();
  bool accept = request.accept();
  std::string replyMessage = request.reply_message();
  // actor 只取入口注入的 canonical uid，不从 session 或 from 推导。
  request.set_from(from);

  rsp.set_uid(to);

  long sessionKey = StoreReplyAddFriend(request);
  if (sessionKey == -1) {
    LOG_ERROR(businessLogger,
              "StoreageReplyAddFriend is failed, from {}, to {}", from, to);
    rsp.set_error(ErrorCodes::MysqlFailed);
    rsp.set_message("数据库修改发送异常");
    return rsp;
  }

  TcpPacket senderRsp{};
  senderRsp.set_from(from);
  senderRsp.set_to(to);
  senderRsp.set_uid(to);
  senderRsp.set_session_key(sessionKey);
  senderRsp.set_accept(accept);
  senderRsp.set_reply_message(replyMessage);
  senderRsp.set_seq(db::RedisDao::GetInstance()->generateMsgId());
  if (clientForwardService.ForwardToGateway(
          to, SerializeTcpPacket(senderRsp), ID_REPLY_ADD_FRIEND_REQ)) {
    rsp.set_error(ErrorCodes::Success);
    return rsp;
  }

  LOG_DEBUG(businessLogger,
            "friend reply stored but target is not on a healthy gateway, from: "
            "{}, to: {}",
            from, to);
  rsp.set_error(ErrorCodes::Success);
  return rsp;
}

int FriendService::StoreReplyAddFriend(TcpPacket &request) {
  long from = request.uid();
  long to = request.to();
  bool accept = request.accept();
  std::string replyMessage = request.reply_message();

  int rt = -1;
  db::FriendApply::Status status{};
  std::string time = getCurrentDateTime();
  long sessionId{};
  if (accept) {
    LOG_INFO(businessLogger, "accept friend request, from {}, to {}", from, to);
    status = db::FriendApply::Status::Agree;
    sessionId = db::RedisDao::GetInstance()->generateSessionId();
    db::Friend::Ptr friendData(new db::Friend(from, to, time, sessionId));

    rt = db::MysqlDao::GetInstance()->insertFriend(friendData);
    if (rt == -1) {
      LOG_ERROR(businessLogger, "insertFriend filed is failed, from {}, to {}",
                from, to);
      return -1;
    }
  } else {
    status = db::FriendApply::Status::Refuse;
  }

  db::FriendApply::Ptr friendApply(
      new db::FriendApply(from, to, status, replyMessage, time));
  rt = db::MysqlDao::GetInstance()->updateFriendApplyStatus(friendApply);
  if (rt == -1) {
    LOG_ERROR(businessLogger,
              "updateFriendApplyStatus filed is failed, from {}, to {}", from,
              to);
  }
  // 暂用
  return rt == -1 ? -1 : sessionId;
}

TcpPacket FriendService::PullFriendApplyList(uint32_t msgID,
                                             TcpPacket &request) {
  TcpPacket rsp;

  int64_t uid = request.uid();
  db::FriendApply::FriendApplyGroup applyList =
      db::MysqlDao::GetInstance()->getFriendApplyList(uid);
  if (applyList == nullptr) {
    LOG_INFO(businessLogger, "回应表为空, uid: {}", uid);
    rsp.set_error(ErrorCodes::Success);
    return rsp;
  }

  for (auto applyObject : *applyList) {
    auto *apply = rsp.add_apply_list();
    apply->set_from(applyObject->from);
    apply->set_to(applyObject->to);
    apply->set_status(applyObject->status);
    apply->set_content(applyObject->content);
    apply->set_apply_date_time(applyObject->createTime);
  }
  rsp.set_error(ErrorCodes::Success);
  return rsp;
}

TcpPacket FriendService::PullFriendList(uint32_t msgID, TcpPacket &request) {
  TcpPacket rsp;

  int64_t uid = request.uid();
  db::Friend::FriendGroup friendList =
      db::MysqlDao::GetInstance()->getFriendList(uid);

  if (friendList == nullptr) {
    LOG_INFO(wimi::businessLogger, "好友表为空, uid-{}", uid);
    rsp.set_error(ErrorCodes::Success);
    return rsp;
  }

  for (auto friendObject : *friendList) {
    int64_t friendUid = friendObject->uidB;
    db::UserInfo::Ptr friendInfo =
        db::MysqlDao::GetInstance()->getUserInfo(friendUid);
    if (friendInfo != nullptr) {
      auto *info = rsp.add_friend_list();
      info->set_uid(friendUid);
      info->set_name(friendInfo->name);
      info->set_age(friendInfo->age);
      info->set_sex(friendInfo->sex);
      info->set_head_image_url(friendInfo->headImageURL);
    }
  }

  rsp.set_error(ErrorCodes::Success);
  return rsp;
}
};  // namespace wimi
