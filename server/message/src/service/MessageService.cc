#include "MessageService.h"

#include "Const.h"
#include "DbGlobal.h"
#include "Logger.h"
#include "Mysql.h"
#include "Redis.h"
#include "RequestContext.h"

#include <string>

namespace wimi {

MessageService::AcceptedText MessageService::AcceptText(TcpPacket request) {
  AcceptedText result;
  const int64_t clientSeq = request.seq();
  const int64_t from = request.uid();
  const int64_t to = request.to();
  const std::string data = request.data();
  const std::string clientMessageId =
      request.has_client_message_id() && !request.client_message_id().empty()
          ? request.client_message_id()
          : std::to_string(clientSeq);

  result.response.set_seq(clientSeq);
  result.response.set_client_message_id(clientMessageId);
  if (from <= 0 || to <= 0 || data.empty() || clientMessageId.empty() ||
      (!request.has_client_message_id() && clientSeq <= 0)) {
    result.response.set_error(ErrorCodes::JsonParser);
    result.response.set_message(
        "actor, to, data and client_message_id are required");
    result.response.set_retryable(false);
    return result;
  }

  const std::string sendDateTime = getCurrentDateTime();
  auto accepted = db::MysqlDao::GetInstance()->acceptDirectText(
      from, to, clientMessageId, data, sendDateTime);
  result.response.set_error(accepted.error);
  result.response.set_retryable(isRetryableError(accepted.error));
  if (accepted.error != ErrorCodes::Success) {
    if (!accepted.diagnostic.empty())
      LOG_ERROR(dbLogger, "direct message accept failed: {}",
                accepted.diagnostic);
    result.response.set_message(accepted.diagnostic.empty()
                                    ? "persistent message accept failed"
                                    : accepted.diagnostic);
    return result;
  }

  result.response.set_status("accepted");
  result.response.set_message_id(accepted.messageId);
  result.response.set_conversation_id(accepted.conversationId);
  result.response.set_conversation_seq(accepted.conversationSeq);
  result.response.set_session_key(accepted.conversationId);
  result.response.set_message_state(protocol::MESSAGE_STATE_ACCEPTED);
  result.response.set_retryable(false);

  request.set_uid(from);
  request.set_from(from);
  request.set_seq(accepted.messageId);
  request.set_message_id(accepted.messageId);
  request.set_client_message_id(clientMessageId);
  request.set_conversation_id(accepted.conversationId);
  request.set_conversation_seq(accepted.conversationSeq);
  request.set_session_key(accepted.conversationId);
  request.set_message_state(protocol::MESSAGE_STATE_ACCEPTED);
  request.set_send_date_time(sendDateTime);
  result.forwardPacket = std::move(request);
  result.recipientUid = to;
  result.shouldForward = !accepted.duplicate;
  return result;
}

MessageService::AcceptedGroupText MessageService::AcceptGroupText(
    TcpPacket request) {
  AcceptedGroupText result;
  const int64_t clientSeq = request.seq();
  const int64_t sender = request.uid();
  const int64_t groupId =
      request.has_gid() ? request.gid()
                        : (request.has_group_id() ? request.group_id() : 0);
  const std::string content = request.data();
  const std::string clientMessageId =
      request.has_client_message_id() && !request.client_message_id().empty()
          ? request.client_message_id()
          : std::to_string(clientSeq);

  result.response.set_seq(clientSeq);
  result.response.set_client_message_id(clientMessageId);
  result.response.set_gid(groupId);
  if (sender <= 0 || groupId <= 0 || content.empty() ||
      clientMessageId.empty() ||
      (!request.has_client_message_id() && clientSeq <= 0)) {
    result.response.set_error(ErrorCodes::JsonParser);
    result.response.set_message(
        "actor, gid, data and client_message_id are required");
    result.response.set_retryable(false);
    return result;
  }

  const std::string sendDateTime = getCurrentDateTime();
  auto accepted = db::MysqlDao::GetInstance()->acceptGroupText(
      sender, groupId, clientMessageId, content, sendDateTime);
  result.response.set_error(accepted.error);
  result.response.set_retryable(isRetryableError(accepted.error));
  if (accepted.error != ErrorCodes::Success) {
    if (!accepted.diagnostic.empty())
      LOG_ERROR(dbLogger, "group message accept failed: {}",
                accepted.diagnostic);
    result.response.set_message(accepted.diagnostic.empty()
                                    ? "persistent group message accept failed"
                                    : accepted.diagnostic);
    return result;
  }

  result.response.set_status("accepted");
  result.response.set_message_id(accepted.messageId);
  result.response.set_conversation_id(accepted.conversationId);
  result.response.set_conversation_seq(accepted.conversationSeq);
  result.response.set_session_key(accepted.conversationId);
  result.response.set_message_state(protocol::MESSAGE_STATE_ACCEPTED);
  result.response.set_retryable(false);

  request.set_from(sender);
  request.set_gid(groupId);
  request.set_seq(accepted.messageId);
  request.set_message_id(accepted.messageId);
  request.set_client_message_id(clientMessageId);
  request.set_conversation_id(accepted.conversationId);
  request.set_conversation_seq(accepted.conversationSeq);
  request.set_session_key(accepted.conversationId);
  request.set_message_state(protocol::MESSAGE_STATE_ACCEPTED);
  request.set_send_date_time(sendDateTime);
  result.forwardPacket = std::move(request);
  result.recipientUids = std::move(accepted.recipientUids);
  result.shouldForward = !accepted.duplicate;
  return result;
}

MessageService::AckResult MessageService::Ack(TcpPacket &request) {
  AckResult result;
  auto &rsp = result.response;
  int64_t seq = request.seq();
  int64_t uid = request.uid();
  if (seq <= 0) {
    rsp.set_error(ErrorCodes::JsonParser);
    return result;
  }
  if (!request.has_receipt_type() ||
      !request.has_conversation_id() || !request.has_conversation_seq() ||
      request.conversation_id() <= 0 || request.conversation_seq() <= 0) {
    rsp.set_error(ErrorCodes::JsonParser);
    return result;
  }
  const auto receiptType = request.receipt_type();
  if (receiptType != protocol::RECEIPT_TYPE_DELIVERED &&
      receiptType != protocol::RECEIPT_TYPE_READ) {
    rsp.set_error(ErrorCodes::JsonParser);
    return result;
  }
  short status = receiptType == protocol::RECEIPT_TYPE_READ
                     ? db::Message::Status::READ
                     : db::Message::Status::DELIVERED;
  std::string readTime = receiptType == protocol::RECEIPT_TYPE_READ
                             ? getCurrentDateTime()
                             : std::string{};

  const auto acknowledged =
      db::MysqlDao::GetInstance()->acknowledgeConversationMessage(
          seq, uid, request.conversation_id(), request.conversation_seq(),
          status, readTime);
  if (acknowledged.updated <= 0) {
    LOG_WARN(wimi::businessLogger,
             "ACK所有权校验失败或消息不存在, seq: {}, principal: {}", seq, uid);
    rsp.set_error(acknowledged.updated == -1
                      ? ErrorCodes::MysqlFailed
                      : ErrorCodes::MessageOwnershipInvalid);
    return result;
  }

  rsp.set_error(ErrorCodes::Success);
  rsp.set_message_id(seq);
  rsp.set_message_state(receiptType == protocol::RECEIPT_TYPE_READ
                            ? protocol::MESSAGE_STATE_READ
                            : protocol::MESSAGE_STATE_DELIVERED);
  if (receiptType == protocol::RECEIPT_TYPE_READ &&
      acknowledged.directConversation && acknowledged.readStateChanged) {
    result.senderUid = acknowledged.senderUid;
    result.shouldForwardRead = result.senderUid > 0;
    result.readReceipt.set_seq(seq);
    result.readReceipt.set_message_id(seq);
    result.readReceipt.set_from(uid);
    result.readReceipt.set_to(result.senderUid);
    result.readReceipt.set_conversation_id(request.conversation_id());
    result.readReceipt.set_conversation_seq(request.conversation_seq());
    result.readReceipt.set_message_state(protocol::MESSAGE_STATE_READ);
  }
  return result;
}

TcpPacket MessageService::SendFile(uint32_t msgID, TcpPacket &request) {
  /* 一面推送消息，一面存储消息，其中离线情况，
  message表中的content对文件消息而言无作用，因其存储在文件系统
  存储规则目前暂为：fileService/uid/[seq].txt
  */
  return {};
}

TcpPacket MessageService::SendGroupText(uint32_t msgID, TcpPacket &request) {
  /*
    1.检查群聊
    2.建立<seq, [member1, member2, ...]>映射
    3.按成员在线状态分发消息
  */
  return {};
}

TcpPacket MessageService::SendText(uint32_t msgID, TcpPacket &request) {
  (void)msgID;
  auto accepted = AcceptText(request);
  return accepted.response;
}

TcpPacket MessageService::PullSessionMessages(uint32_t msgID,
                                              TcpPacket &request) {
  TcpPacket rsp;

  if (request.has_conversation_id()) {
    const int64_t conversationId = request.conversation_id();
    const int64_t afterSeq = request.has_after_seq() ? request.after_seq() : 0;
    const int limit = request.has_limit() ? request.limit() : 50;
    auto sync = db::MysqlDao::GetInstance()->syncConversation(
        request.uid(), conversationId, afterSeq, limit);
    rsp.set_error(sync.error);
    rsp.set_conversation_id(conversationId);
    rsp.set_latest_seq(sync.latestSeq);
    rsp.set_next_seq(sync.nextSeq);
    rsp.set_has_more(sync.hasMore);
    if (sync.conversationType == 1)
      rsp.set_conversation_type(protocol::CONVERSATION_TYPE_DIRECT);
    else if (sync.conversationType == 2)
      rsp.set_conversation_type(protocol::CONVERSATION_TYPE_GROUP);
    if (sync.error != ErrorCodes::Success)
      return rsp;
    for (const auto &message : sync.messages) {
      auto *item = rsp.add_message_list();
      item->set_message_id(message.messageId);
      item->set_from(message.senderId);
      item->set_to(message.receiverId);
      item->set_conversation_id(message.conversationId);
      item->set_conversation_seq(message.conversationSeq);
      item->set_client_message_id(message.clientMessageId);
      item->set_type(message.type);
      item->set_content(message.content);
      item->set_status(message.status);
      item->set_send_date_time(message.sendDateTime);
      item->set_read_date_time(message.readDateTime);
    }
    return rsp;
  }

  int64_t from = request.from();
  int64_t to = request.to();
  int64_t lastMsgId = request.last_msg_id();
  int limit = request.limit();

  auto messageList = db::MysqlDao::GetInstance()->getSessionMessage(
      from, to, lastMsgId, limit);
  if (messageList == nullptr) {
    LOG_INFO(wimi::businessLogger,
             "消息表为空, from: {}, to: {}, lastMsgId: {}, limit: {}", from, to,
             lastMsgId, limit);

    rsp.set_error(ErrorCodes::Success);
    return rsp;
  }

  rsp.set_uid(to);
  for (auto message : *messageList) {
    auto *item = rsp.add_message_list();
    item->set_message_id(message->messageId);
    item->set_type(message->type);
    item->set_content(message->content);
    item->set_status(message->status);
    item->set_send_date_time(message->sendDateTime);
    item->set_read_date_time(message->readDateTime);
  }
  rsp.set_error(ErrorCodes::Success);
  return rsp;
}

TcpPacket MessageService::PullMessages(uint32_t msgID, TcpPacket &request) {
  TcpPacket rsp;

  int64_t uid = request.uid();
  int64_t lastMsgId = request.last_msg_id();
  int limit = request.limit();

  rsp.set_uid(uid);

  auto messageList =
      db::MysqlDao::GetInstance()->getUserMessage(uid, lastMsgId, limit);
  if (messageList == nullptr) {
    LOG_INFO(wimi::businessLogger,
             "消息表为空,  uid: {}, lastMsgId: {}, limit: {}", uid, lastMsgId,
             limit);

    rsp.set_error(ErrorCodes::Success);
    return rsp;
  }

  for (auto message : *messageList) {
    auto *item = rsp.add_message_list();
    item->set_message_id(message->messageId);
    item->set_type(message->type);
    item->set_content(message->content);
    item->set_status(message->status);
    item->set_send_date_time(message->sendDateTime);
    item->set_read_date_time(message->readDateTime);
  }
  rsp.set_error(ErrorCodes::Success);
  return rsp;
}

}  // namespace wimi
