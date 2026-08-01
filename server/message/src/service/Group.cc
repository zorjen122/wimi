#include "GroupService.h"

#include "Const.h"
#include "DbGlobal.h"
#include "ClientForwardService.h"
#include "Logger.h"
#include "Mysql.h"
#include "Redis.h"
#include <cstdint>
#include <spdlog/spdlog.h>
namespace wimi
{

GroupService::GroupService(ClientForwardService& clientForwardService) : clientForwardService(clientForwardService) {}

TcpPacket GroupService::Create(unsigned int msgID, TcpPacket& request)
{
    TcpPacket rsp;

    int ret{};

    int64_t uid = request.uid();
    std::string groupName = request.group_name();

    int64_t groupId = db::RedisDao::GetInstance()->generateGroupId();
    int64_t conversationId = db::RedisDao::GetInstance()->generateConversationId();
    rsp.set_group_id(groupId);
    rsp.set_conversation_id(conversationId);

    std::string createTime = getCurrentDateTime();
    db::GroupManager::Ptr group(new db::GroupManager(groupId, conversationId, groupName, createTime));

    ret = db::MysqlDao::GetInstance()->insertGroup(group);
    if (ret == -1) {
        rsp.set_error(ErrorCodes::MysqlFailed);
        rsp.set_message("数据库操作失败");
        return rsp;
    }

    db::GroupMember::Ptr member(new db::GroupMember(groupId, uid, db::GroupMember::Role::Master, createTime));
    ret = db::MysqlDao::GetInstance()->insertGroupMember(member);
    if (ret == -1) {
        rsp.set_error(ErrorCodes::MysqlFailed);
        rsp.set_message("数据库操作失败");
        return rsp;
    }

    rsp.set_error(ErrorCodes::Success);
    return rsp;
}

int GroupService::NotifyMemberJoin(int64_t uid, int64_t groupId, const std::string& requestMessage)
{
    db::GroupMember::MemberList managerList =
        db::MysqlDao::GetInstance()->getGroupRoleMemberList(groupId, db::GroupMember::Role::Manager);
    for (auto manager : managerList) {
        int64_t serverSeq = db::RedisDao::GetInstance()->generateMsgId();
        TcpPacket notifyRequest;
        notifyRequest.set_uid(uid);
        notifyRequest.set_group_id(groupId);
        notifyRequest.set_content(requestMessage);
        notifyRequest.set_seq(serverSeq);
        notifyRequest.set_error(ErrorCodes::Success);
        if (clientForwardService.ForwardToGateway(manager->uid, SerializeTcpPacket(notifyRequest),
                                                  ID_GROUP_NOTIFY_JOIN_REQ, serverSeq))
            continue;

        LOG_DEBUG(businessLogger,
                  "group join notify stored but manager is not on a healthy "
                  "gateway, groupId: {}, manager_uid: {}",
                  groupId, manager->uid);
    }
    return 0;
}

TcpPacket GroupService::NotifyJoin(unsigned int msgID, TcpPacket& request)
{
    TcpPacket rsp;

    int64_t uid = request.uid();
    int64_t groupId = request.group_id();
    std::string requestMessage = request.request_message();
    // 搜索群聊时存在，但加入群聊时未必存在，此处仍有一致性问题，后续待拟
    int ret = db::MysqlDao::GetInstance()->hasGroup(groupId);
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
    db::GroupApply::Ptr apply(new db::GroupApply(uid, 0, groupId, db::GroupApply::Type::Add,
                                                 db::GroupApply::Status::Wait, requestMessage, createTime));

    ret = db::MysqlDao::GetInstance()->insertGroupApply(apply);
    if (ret == -1) {
        rsp.set_error(ErrorCodes::MysqlFailed);
        rsp.set_message("数据库操作失败");
        return rsp;
    }

    ret = NotifyMemberJoin(uid, groupId, requestMessage);
    if (ret == -1) {
        rsp.set_error(ErrorCodes::GroupNotExists);
        rsp.set_message("通知群成员失败");
        return rsp;
    }

    rsp.set_error(ErrorCodes::Success);
    rsp.set_message("通知群成员成功");

    return rsp;
}

TcpPacket GroupService::PullNotify(unsigned int msgID, TcpPacket& request)
{
    TcpPacket rsp;
    rsp.set_group_id(request.group_id());

    int64_t groupId = request.group_id();

    int ret = db::MysqlDao::GetInstance()->hasGroup(groupId);
    if (ret == false) {
        rsp.set_error(ErrorCodes::GroupNotExists);
        rsp.set_message("群组已不存在");
    }

    db::FriendApply::FriendApplyGroup applyList = db::MysqlDao::GetInstance()->getFriendApplyList(groupId);
    if (applyList == nullptr) {
        rsp.set_error(ErrorCodes::MysqlFailed);
        rsp.set_message("数据库操作失败");
        return rsp;
    }

    for (auto apply : *applyList) {
        auto* notifyInfo = rsp.add_apply_list();
        notifyInfo->set_to(apply->to);
    }

    rsp.set_error(ErrorCodes::Success);
    return rsp;
}
int GroupService::NotifyMemberReply(int64_t groupId, int64_t managerUid, int64_t requestorUid, bool accept)
{
    db::GroupMember::MemberList memberList = db::MysqlDao::GetInstance()->getGroupMemberList(groupId);
    for (auto member : memberList) {
        int64_t serverSeq = db::RedisDao::GetInstance()->generateMsgId();
        TcpPacket notifyRequest;
        notifyRequest.set_requestor_uid(requestorUid);
        notifyRequest.set_replyor_uid(managerUid);
        notifyRequest.set_group_id(groupId);
        notifyRequest.set_accept(accept);
        notifyRequest.set_seq(serverSeq);
        if (clientForwardService.ForwardToGateway(member->uid, SerializeTcpPacket(notifyRequest),
                                                  ID_GROUP_REPLY_JOIN_REQ, serverSeq))
            continue;

        LOG_DEBUG(businessLogger,
                  "group reply notify stored but member is not on a healthy "
                  "gateway, groupId: {}, member_uid: {}",
                  groupId, member->uid);
    }

    return 0;
}
TcpPacket GroupService::ReplyJoin(unsigned int msgID, TcpPacket& request)
{
    TcpPacket rsp;
    rsp.set_group_id(request.group_id());

    int64_t groupId = request.group_id();
    // 群管理操作的 actor 来自入口注入的 principal，而非客户端 manager_uid。
    int64_t managerUid = request.uid();
    int64_t requestorUid = request.requestor_uid();
    bool accept = request.accept();

    // 搜索群聊时存在，但加入群聊时未必存在
    int ret = db::MysqlDao::GetInstance()->hasGroup(groupId);
    if (ret == 0) {
        rsp.set_error(ErrorCodes::GroupNotExists);
        rsp.set_message("群组不存在");
        return rsp;
    }

    // 暂用好友相关表，消息将通知所有群管理
    std::string createTime = getCurrentDateTime();
    db::GroupApply::Ptr apply(
        new db::GroupApply(requestorUid, managerUid, groupId, db::GroupApply::Type::Add,
                           accept ? db::GroupApply::Status::Agree : db::GroupApply::Status::Refuse, "", createTime));

    // 在之前应有检查是否已经同意过，待实现
    ret = db::MysqlDao::GetInstance()->updateGroupApply(apply);
    if (ret == -1) {
        rsp.set_error(ErrorCodes::MysqlFailed);
        rsp.set_message("数据库操作失败");
        return rsp;
    }

    if (accept) {
        db::GroupMember::Ptr member(
            new db::GroupMember(groupId, requestorUid, db::GroupMember::Role::Member, createTime));
        ret = db::MysqlDao::GetInstance()->insertGroupMember(member);
        if (ret == -1) {
            rsp.set_error(ErrorCodes::MysqlFailed);
            rsp.set_message("数据库操作失败");
            return rsp;
        }
    }

    // 此时请求者已成为群成员，通知所有群成员中包含请求者
    ret = NotifyMemberReply(groupId, managerUid, requestorUid, accept);
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

TcpPacket GroupService::Quit(unsigned int msgID, TcpPacket& request)
{
    TcpPacket rsp;
    return rsp;
}

}; // namespace wimi
