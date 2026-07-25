#include "GroupService.h"

#include "Const.h"
#include "DbGlobal.h"
#include "ClientForwardService.h"
#include "Logger.h"
#include "Mysql.h"
#include "Redis.h"
#include <cstdint>
#include <spdlog/spdlog.h>
namespace wimi {

GroupService::GroupService(ClientForwardService &clientForwardService)
    : clientForwardService(clientForwardService) {}

TcpPacket GroupService::Create(unsigned int msgID, TcpPacket &request) {
  TcpPacket rsp;

  int ret{};

  // gid should by server generate, todo...
  int64_t uid = request.uid();
  std::string groupName = request.group_name();

  int64_t gid = db::RedisDao::GetInstance()->generateMsgId();
  rsp.set_gid(gid);

  int64_t sessionKey = db::RedisDao::GetInstance()->generateSessionId();
  std::string createTime = getCurrentDateTime();
  db::GroupManager::Ptr group(
      new db::GroupManager(gid, sessionKey, groupName, createTime));

  ret = db::MysqlDao::GetInstance()->insertGroup(group);
  if (ret == -1) {
    rsp.set_error(ErrorCodes::MysqlFailed);
    rsp.set_message("数据库操作失败");
    return rsp;
  }

  db::GroupMember::Ptr member(
      new db::GroupMember(gid, uid, db::GroupMember::Role::Master, createTime));
  ret = db::MysqlDao::GetInstance()->insertGroupMember(member);
  if (ret == -1) {
    rsp.set_error(ErrorCodes::MysqlFailed);
    rsp.set_message("数据库操作失败");
    return rsp;
  }

  rsp.set_error(ErrorCodes::Success);
  rsp.set_session_key(sessionKey);
  return rsp;
}

int GroupService::NotifyMemberJoin(int64_t uid, int64_t gid,
                                   const std::string &requestMessage) {
  db::GroupMember::MemberList managerList =
      db::MysqlDao::GetInstance()->getGroupRoleMemberList(
          gid, db::GroupMember::Role::Manager);
  for (auto manager : managerList) {
    int64_t serverSeq = db::RedisDao::GetInstance()->generateMsgId();
    TcpPacket notifyRequest;
    notifyRequest.set_uid(uid);
    notifyRequest.set_gid(gid);
    notifyRequest.set_content(requestMessage);
    notifyRequest.set_seq(serverSeq);
    notifyRequest.set_error(ErrorCodes::Success);
    if (clientForwardService.ForwardToGateway(
            manager->uid, SerializeTcpPacket(notifyRequest),
            ID_GROUP_NOTIFY_JOIN_REQ, serverSeq))
      continue;

    LOG_DEBUG(businessLogger,
              "group join notify stored but manager is not on a healthy "
              "gateway, gid: {}, manager_uid: {}",
              gid, manager->uid);
  }
  return 0;
}

TcpPacket GroupService::NotifyJoin(unsigned int msgID, TcpPacket &request) {
  TcpPacket rsp;

  int64_t uid = request.uid();
  int64_t gid = request.gid();
  std::string requestMessage = request.request_message();
  // 搜索群聊时存在，但加入群聊时未必存在，此处仍有一致性问题，后续待拟
  int ret = db::MysqlDao::GetInstance()->hasGroup(gid);
  if (ret == 0) {
    rsp.set_error(ErrorCodes::GroupNotExists);
    rsp.set_message("群组不存在");
    return rsp;
  } else if (ret == -1) {
    rsp.set_error(ErrorCodes::MysqlFailed);
    rsp.set_message("数据库操作失败");
    return rsp;
  }

  // 暂用好友相关表，消息将通知所有群管理
  std::string createTime = getCurrentDateTime();
  db::GroupApply::Ptr apply(new db::GroupApply(
      uid, 0, gid, db::GroupApply::Type::Add, db::GroupApply::Status::Wait,
      requestMessage, createTime));

  ret = db::MysqlDao::GetInstance()->insertGroupApply(apply);
  if (ret == -1) {
    rsp.set_error(ErrorCodes::MysqlFailed);
    rsp.set_message("数据库操作失败");
    return rsp;
  }

  ret = NotifyMemberJoin(uid, gid, requestMessage);
  if (ret == -1) {
    rsp.set_error(ErrorCodes::GroupNotExists);
    rsp.set_message("通知群成员失败");
    return rsp;
  }

  rsp.set_error(ErrorCodes::Success);
  rsp.set_message("通知群成员成功");

  return rsp;
}

TcpPacket GroupService::PullNotify(unsigned int msgID, TcpPacket &request) {
  TcpPacket rsp;
  rsp.set_gid(request.gid());

  int64_t gid = request.gid();

  int ret = db::MysqlDao::GetInstance()->hasGroup(gid);
  if (ret == false) {
    rsp.set_error(ErrorCodes::GroupNotExists);
    rsp.set_message("群组已不存在");
  }

  db::FriendApply::FriendApplyGroup applyList =
      db::MysqlDao::GetInstance()->getFriendApplyList(gid);
  if (applyList == nullptr) {
    rsp.set_error(ErrorCodes::MysqlFailed);
    rsp.set_message("数据库操作失败");
    return rsp;
  }

  for (auto apply : *applyList) {
    auto *notifyInfo = rsp.add_apply_list();
    notifyInfo->set_to(apply->to);
  }

  rsp.set_error(ErrorCodes::Success);
  return rsp;
}
int GroupService::NotifyMemberReply(int64_t gid, int64_t managerUid,
                                    int64_t requestorUid, bool accept) {
  db::GroupMember::MemberList memberList =
      db::MysqlDao::GetInstance()->getGroupMemberList(gid);
  for (auto member : memberList) {
    int64_t serverSeq = db::RedisDao::GetInstance()->generateMsgId();
    TcpPacket notifyRequest;
    notifyRequest.set_requestor_uid(requestorUid);
    notifyRequest.set_replyor_uid(managerUid);
    notifyRequest.set_gid(gid);
    notifyRequest.set_accept(accept);
    notifyRequest.set_seq(serverSeq);
    if (clientForwardService.ForwardToGateway(
            member->uid, SerializeTcpPacket(notifyRequest),
            ID_GROUP_REPLY_JOIN_REQ, serverSeq))
      continue;

    LOG_DEBUG(businessLogger,
              "group reply notify stored but member is not on a healthy "
              "gateway, gid: {}, member_uid: {}",
              gid, member->uid);
  }

  return 0;
}
TcpPacket GroupService::ReplyJoin(unsigned int msgID, TcpPacket &request) {
  TcpPacket rsp;
  rsp.set_gid(request.gid());

  int64_t gid = request.gid();
  // 群管理操作的 actor 来自入口注入的 principal，而非客户端 manager_uid。
  int64_t managerUid = request.uid();
  int64_t requestorUid = request.requestor_uid();
  bool accept = request.accept();

  // 搜索群聊时存在，但加入群聊时未必存在
  int ret = db::MysqlDao::GetInstance()->hasGroup(gid);
  if (ret == 0) {
    rsp.set_error(ErrorCodes::GroupNotExists);
    rsp.set_message("群组不存在");
    return rsp;
  }

  // 暂用好友相关表，消息将通知所有群管理
  std::string createTime = getCurrentDateTime();
  db::GroupApply::Ptr apply(new db::GroupApply(
      requestorUid, managerUid, gid, db::GroupApply::Type::Add,
      db::GroupApply::Status::Agree, "", createTime));

  // 在之前应有检查是否已经同意过，待实现
  ret = db::MysqlDao::GetInstance()->updateGroupApply(apply);
  if (ret == -1) {
    rsp.set_error(ErrorCodes::MysqlFailed);
    rsp.set_message("数据库操作失败");
    return rsp;
  }

  db::GroupMember::Ptr member(new db::GroupMember(
      gid, requestorUid, db::GroupMember::Role::Member, createTime));
  ret = db::MysqlDao::GetInstance()->insertGroupMember(member);
  if (ret == -1) {
    rsp.set_error(ErrorCodes::MysqlFailed);
    rsp.set_message("数据库操作失败");
    return rsp;
  }

  // 此时请求者已成为群成员，通知所有群成员中包含请求者
  ret = NotifyMemberReply(gid, managerUid, requestorUid, accept);
  if (ret == -1) {
    rsp.set_error(ErrorCodes::GroupReplyFailed);
    rsp.set_message("通知群成员失败");
    return rsp;
  }

  if (!accept) {
    rsp.set_error(ErrorCodes::Success);
    rsp.set_message("拒绝该用户加入群组请求");
    return rsp;
  }

  rsp.set_error(ErrorCodes::Success);
  rsp.set_message("同意加入群组");
  return rsp;
}

TcpPacket GroupService::Quit(unsigned int msgID, TcpPacket &request) {
  TcpPacket rsp;
  return rsp;
}

};  // namespace wimi
