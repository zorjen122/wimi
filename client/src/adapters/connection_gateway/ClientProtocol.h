#pragma once

#include <QtGlobal>

namespace wimi::client::protocol {

enum ServiceId : quint32 {
  PullFriendListRequest = 1001,
  PullFriendListResponse = 1002,
  PullFriendApplyListRequest = 1003,
  PullFriendApplyListResponse = 1004,
  PullSessionMessagesRequest = 1005,
  PullSessionMessagesResponse = 1006,
  PullMessagesRequest = 1007,
  PullMessagesResponse = 1008,
  PullGroupMembersRequest = 1009,
  PullGroupMembersResponse = 1010,
  PingRequest = 1011,
  PingResponse = 1012,
  LoginRequest = 1013,
  LoginResponse = 1014,
  QuitRequest = 1015,
  QuitResponse = 1016,
  InitUserInfoRequest = 1017,
  InitUserInfoResponse = 1018,
  SearchUserRequest = 1019,
  SearchUserResponse = 1020,
  AddFriendRequest = 1021,
  AddFriendResponse = 1022,
  ReplyFriendRequest = 1023,
  ReplyFriendResponse = 1024,
  RemoveFriendRequest = 1025,
  RemoveFriendResponse = 1026,
  SendTextRequest = 1027,
  SendTextResponse = 1028,
  SendFileRequest = 1029,
  SendFileResponse = 1030,
  UploadFileRequest = 1031,
  UploadFileResponse = 1032,
  Ack = 1033,
  NullResponse = 1034,
  CreateGroupRequest = 1035,
  CreateGroupResponse = 1036,
  JoinGroupRequest = 1037,
  JoinGroupResponse = 1038,
  ReplyJoinGroupRequest = 1039,
  ReplyJoinGroupResponse = 1040,
  QuitGroupRequest = 1041,
  QuitGroupResponse = 1042,
  SendGroupTextRequest = 1043,
  SendGroupTextResponse = 1044,
  TextReadReceiptNotification = 1045,
  TextReadReceiptNotificationResponse = 1046,
};

enum ErrorCode : int {
  Success = 0,
  JsonParser = 1000,
  RpcFailed = 1001,
  VerifyExpired = 1002,
  VerifyCodeError = 1003,
  UserExists = 1004,
  PasswordError = 1005,
  EmailMismatch = 1006,
  PasswordUpdateFailed = 1007,
  PasswordInvalid = 1008,
  TokenInvalid = 1009,
  UidInvalid = 1010,
  UserNotOnline = 1011,
  UserNotFriend = 1012,
  UserOnline = 1013,
  UserOffline = 1014,
  NotFound = 1015,
  RepeatMessage = 1016,
  MysqlFailed = 1017,
  FileTypeError = 1018,
  InternalError = 1019,
  GroupAlreadyExists = 1020,
  GroupNotExists = 1021,
  GroupNotifyFailed = 1022,
  GroupReplyFailed = 1023,
  AuthenticationRequired = 1024,
  MessageOwnershipInvalid = 1025,
  DeadlineExceeded = 1026,
  ResourceExhausted = 1027,
  DependencyUnavailable = 1028,
  IdempotencyConflict = 1029,
};

constexpr quint32 ResponseFor(quint32 requestServiceId) {
  return requestServiceId == Ack ? NullResponse : requestServiceId + 1;
}

constexpr bool IsRetryableError(int code) {
  return code == RpcFailed || code == MysqlFailed || code == InternalError ||
         code == DeadlineExceeded || code == ResourceExhausted ||
         code == DependencyUnavailable;
}

}  // namespace wimi::client::protocol
