#include "app/AppController.h"

#include "adapters/connection_gateway/ClientProtocol.h"
#include "adapters/connection_gateway/ProtobufPacketCodec.h"
#include "adapters/fake/FakeScenarioRepository.h"
#include "adapters/sqlite/SqliteClientRepository.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>

#include <algorithm>

namespace wimi::client {
namespace {

QString DirectConversationId(std::int64_t peerUid) {
  return QStringLiteral("direct:%1").arg(peerUid);
}

QString GroupConversationId(std::int64_t groupId) {
  return QStringLiteral("group:%1").arg(groupId);
}

std::optional<std::int64_t> DirectPeerUid(const QString &conversationId) {
  constexpr auto prefix = "direct:";
  if (!conversationId.startsWith(QLatin1String(prefix))) {
    return std::nullopt;
  }
  bool valid = false;
  const auto uid = conversationId.mid(sizeof(prefix) - 1).toLongLong(&valid);
  return valid && uid > 0 ? std::optional<std::int64_t>(uid) : std::nullopt;
}

std::optional<std::int64_t> GroupId(const QString &conversationId) {
  constexpr auto prefix = "group:";
  if (!conversationId.startsWith(QLatin1String(prefix))) {
    return std::nullopt;
  }
  bool valid = false;
  const auto groupId =
      conversationId.mid(sizeof(prefix) - 1).toLongLong(&valid);
  return valid && groupId > 0 ? std::optional<std::int64_t>(groupId)
                              : std::nullopt;
}

QString DisplayTimestamp(const QString &serverTimestamp) {
  if (serverTimestamp.isEmpty()) {
    return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"));
  }
  return serverTimestamp.size() >= 5 ? serverTimestamp.right(8).left(5)
                                     : serverTimestamp;
}

}  // namespace

AppController::AppController(QObject *parent)
    : AppController(CreatePlatformServices(), parent) {}

AppController::AppController(
    std::unique_ptr<IPlatformServices> platformServices, QObject *parent)
    : QObject(parent), platform_services_(std::move(platformServices)) {
  if (platform_services_ == nullptr) {
    platform_services_ = CreatePlatformServices();
  }
  requested_window_width_ =
      ArgumentValue(QStringLiteral("width"), QStringLiteral("1280")).toInt();
  requested_window_height_ =
      ArgumentValue(QStringLiteral("height"), QStringLiteral("800")).toInt();
  dark_theme_requested_ =
      ArgumentValue(QStringLiteral("theme"), QStringLiteral("light")) ==
      QStringLiteral("dark");
  compact_conversation_requested_ = QCoreApplication::arguments().contains(
      QStringLiteral("--open-conversation"));
  QString gateUrl = ArgumentValue(QStringLiteral("gate-url"), QString{});
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
  if (gateUrl.isEmpty()) {
    gateUrl = QSettings().value(QStringLiteral("network/gateUrl")).toString();
  }
#endif
  gate_url_ = gateUrl.trimmed();
  network_enabled_ = !gate_url_.isEmpty();
  const QString repositoryKind = ArgumentValue(
      QStringLiteral("repository"),
      network_enabled_ ? QStringLiteral("sqlite") : QStringLiteral("fake"));
  if (repositoryKind == QStringLiteral("sqlite")) {
    QString databasePath = ArgumentValue(QStringLiteral("database"), QString{});
    if (databasePath.isEmpty()) {
      QString account =
          ArgumentValue(QStringLiteral("account"), QStringLiteral("demo"));
      account.replace(QRegularExpression(QStringLiteral("[^a-zA-Z0-9_-]")),
                      QStringLiteral("_"));
      QString dataDirectory =
          ArgumentValue(QStringLiteral("data-dir"), QString{});
      if (dataDirectory.isEmpty()) {
        dataDirectory = QStandardPaths::writableLocation(
            QStandardPaths::AppLocalDataLocation);
      }
      databasePath =
          QDir(dataDirectory)
              .filePath(QStringLiteral("accounts/%1.sqlite").arg(account));
    }
    repository_ = std::make_unique<SqliteClientRepository>(
        std::move(databasePath), !network_enabled_);
  } else {
    repository_ = std::make_unique<FakeScenarioRepository>();
  }

  LoadScenario(
      ArgumentValue(QStringLiteral("scenario"), QStringLiteral("normal")));
  if (network_enabled_) {
    ConfigureNetwork(gate_url_);
  }
  setCurrentSection(
      ArgumentValue(QStringLiteral("section"), QStringLiteral("chats")));
}

AppController::~AppController() = default;

ConversationListModel *AppController::conversations() {
  return &conversations_;
}

MessageListModel *AppController::messages() {
  return &messages_;
}

ContactListModel *AppController::contacts() {
  return &contacts_;
}

RequestListModel *AppController::requests() {
  return &requests_;
}

QStringList AppController::scenarios() const {
  return repository_->ScenarioNames();
}

QString AppController::scenarioName() const {
  return snapshot_.scenarioName;
}

QString AppController::connectionStatus() const {
  return snapshot_.connectionStatus;
}

int AppController::selectedConversationIndex() const {
  return selected_conversation_index_;
}

QString AppController::selectedConversationTitle() const {
  const auto *record = conversations_.RecordAt(selected_conversation_index_);
  return record == nullptr ? QString{} : record->title;
}

bool AppController::selectedConversationIsGroup() const {
  return CurrentConversationId().startsWith(QStringLiteral("group:"));
}

int AppController::selectedConversationUnreadCount() const {
  const auto *record = conversations_.RecordAt(selected_conversation_index_);
  return record == nullptr ? 0 : record->unreadCount;
}

QString AppController::draftText() const {
  return drafts_.value(CurrentStateKey());
}

QString AppController::currentSection() const {
  return current_section_;
}

int AppController::selectedContactIndex() const {
  return selected_contact_index_;
}

QString AppController::selectedContactName() const {
  const auto *record = contacts_.RecordAt(selected_contact_index_);
  return record == nullptr ? QString{} : record->displayName;
}

QString AppController::selectedContactStatus() const {
  const auto *record = contacts_.RecordAt(selected_contact_index_);
  return record == nullptr ? QString{} : record->statusText;
}

bool AppController::authRequired() const {
  return !authentication_completed_ &&
         (network_enabled_ ||
          snapshot_.connectionStatus == QStringLiteral("auth-expired"));
}

bool AppController::networkEnabled() const {
  return network_enabled_;
}

bool AppController::authenticationBusy() const {
  return authentication_busy_;
}

QString AppController::authenticationError() const {
  return authentication_error_;
}

QString AppController::serviceActionStatus() const {
  return service_action_status_;
}

QString AppController::gateUrl() const {
  return gate_url_;
}

QString AppController::gateConfigurationStatus() const {
  return gate_configuration_status_;
}

int AppController::requestedWindowWidth() const {
  return requested_window_width_;
}

int AppController::requestedWindowHeight() const {
  return requested_window_height_;
}

bool AppController::darkThemeRequested() const {
  return dark_theme_requested_;
}

bool AppController::compactConversationRequested() const {
  return compact_conversation_requested_;
}

QString AppController::repositoryKind() const {
  return repository_->RepositoryKind();
}

QString AppController::platformName() const {
  return platform_services_->PlatformName();
}

bool AppController::desktopNotificationsAvailable() const {
  return platform_services_->DesktopNotificationsAvailable();
}

QString AppController::notificationTestStatus() const {
  return notification_test_status_;
}

void AppController::setScenarioName(const QString &scenarioName) {
  if (snapshot_.scenarioName == scenarioName ||
      !repository_->ScenarioNames().contains(scenarioName)) {
    return;
  }
  LoadScenario(scenarioName);
}

void AppController::setDraftText(const QString &draftText) {
  const QString key = CurrentStateKey();
  if (key.isEmpty() || drafts_.value(key) == draftText) {
    return;
  }
  drafts_[key] = draftText;
  repository_->StoreDraft(CurrentConversationId(), draftText);
  emit draftTextChanged();
}

void AppController::setCurrentSection(const QString &currentSection) {
  static const QStringList sections = {
      QStringLiteral("chats"),
      QStringLiteral("contacts"),
      QStringLiteral("requests"),
      QStringLiteral("settings"),
  };
  if (current_section_ == currentSection ||
      !sections.contains(currentSection)) {
    return;
  }
  current_section_ = currentSection;
  emit currentSectionChanged();
}

void AppController::selectConversation(int index) {
  const auto *record = conversations_.RecordAt(index);
  if (record == nullptr) {
    return;
  }

  selected_conversation_index_ = index;
  messages_.SetRecords(snapshot_.messagesByConversation.value(
                           record->conversationId, QVector<MessageRecord>{}),
                       record->unreadCount);
  emit selectedConversationChanged();
  emit draftTextChanged();
  markRead(index);
}

void AppController::sendMessage(const QString &text) {
  const QString trimmed = text.trimmed();
  const auto *conversation =
      conversations_.RecordAt(selected_conversation_index_);
  if (trimmed.isEmpty() || conversation == nullptr) {
    return;
  }

  const auto clientMessageId = next_client_message_id_--;
  const QString timestamp =
      QDateTime::currentDateTime().toString(QStringLiteral("HH:mm"));
  MessageRecord message{
      .clientMessageId = clientMessageId,
      .senderId = QStringLiteral("me"),
      .body = trimmed,
      .timestamp = timestamp,
      .outgoing = true,
      .deliveryState = MessageDeliveryState::PendingLocal,
  };

  if (!repository_->EnqueueOutgoing(conversation->conversationId, message)) {
    message.deliveryState = MessageDeliveryState::PermanentFailed;
  }
  const bool shouldSchedule =
      message.deliveryState == MessageDeliveryState::PendingLocal;

  snapshot_.messagesByConversation[conversation->conversationId].push_back(
      message);
  messages_.Append(std::move(message));
  conversations_.UpdatePreview(conversation->conversationId, trimmed,
                               timestamp);
  for (auto &storedConversation : snapshot_.conversations) {
    if (storedConversation.conversationId != conversation->conversationId) {
      continue;
    }
    storedConversation.preview = trimmed;
    storedConversation.timestamp = timestamp;
    break;
  }
  drafts_.remove(CurrentStateKey());
  emit draftTextChanged();
  if (shouldSchedule && network_enabled_) {
    const auto peerUid = DirectPeerUid(conversation->conversationId);
    const auto groupId = GroupId(conversation->conversationId);
    if (!peerUid.has_value() && !groupId.has_value()) {
      UpdateMessageState(clientMessageId,
                         MessageDeliveryState::PermanentFailed);
      SetServiceActionStatus(tr("当前会话没有可用的服务端接收者"));
      return;
    }
    if (!gateway_client_.IsReady()) {
      UpdateMessageState(clientMessageId, MessageDeliveryState::Unknown);
      SetServiceActionStatus(tr("连接尚未就绪，消息保留为待确认"));
      return;
    }
    UpdateMessageState(clientMessageId, MessageDeliveryState::WaitingAccept);
    const QString wireClientMessageId =
        QStringLiteral("%1:%2").arg(authenticated_uid_).arg(-clientMessageId);
    const std::int64_t remoteConversationId =
        conversation->remoteConversationId.value_or(0);
    const QString requestId =
        peerUid.has_value()
            ? gateway_client_.SendText(*peerUid, trimmed.toUtf8(),
                                       wireClientMessageId,
                                       remoteConversationId)
            : gateway_client_.SendGroupText(*groupId, trimmed.toUtf8(),
                                            wireClientMessageId,
                                            remoteConversationId);
    outgoing_request_messages_.insert(requestId, clientMessageId);
  } else if (shouldSchedule) {
    ScheduleDeliveryLifecycle(clientMessageId);
  }
}

void AppController::retryMessage(qlonglong clientMessageId) {
  QString conversationId;
  QString body;
  for (auto iterator = snapshot_.messagesByConversation.begin();
       iterator != snapshot_.messagesByConversation.end(); ++iterator) {
    const auto found =
        std::find_if(iterator->begin(), iterator->end(),
                     [clientMessageId](const MessageRecord &message) {
                       return message.clientMessageId == clientMessageId;
                     });
    if (found == iterator->end()) {
      continue;
    }
    if (!found->outgoing ||
        found->deliveryState != MessageDeliveryState::RetryableFailed) {
      SetServiceActionStatus(tr("当前消息不需要手动重试"));
      return;
    }
    conversationId = iterator.key();
    body = found->body;
    break;
  }

  if (conversationId.isEmpty()) {
    SetServiceActionStatus(tr("未找到需要重试的消息"));
    return;
  }

  if (!network_enabled_) {
    UpdateMessageState(clientMessageId, MessageDeliveryState::PendingLocal);
    ScheduleDeliveryLifecycle(clientMessageId);
    return;
  }
  if (!gateway_client_.IsReady()) {
    SetServiceActionStatus(tr("连接尚未就绪，请稍后重试"));
    return;
  }

  const auto peerUid = DirectPeerUid(conversationId);
  const auto groupId = GroupId(conversationId);
  if (!peerUid.has_value() && !groupId.has_value()) {
    SetServiceActionStatus(tr("当前会话没有可用的服务端接收者"));
    return;
  }

  const int conversationIndex = conversations_.IndexOf(conversationId);
  const auto *conversation = conversations_.RecordAt(conversationIndex);
  const std::int64_t remoteConversationId =
      conversation == nullptr ? 0
                              : conversation->remoteConversationId.value_or(0);
  UpdateMessageState(clientMessageId, MessageDeliveryState::WaitingAccept);
  const QString wireClientMessageId =
      QStringLiteral("%1:%2").arg(authenticated_uid_).arg(-clientMessageId);
  const QString requestId =
      peerUid.has_value()
          ? gateway_client_.SendText(*peerUid, body.toUtf8(),
                                     wireClientMessageId, remoteConversationId)
          : gateway_client_.SendGroupText(*groupId, body.toUtf8(),
                                          wireClientMessageId,
                                          remoteConversationId);
  outgoing_request_messages_.insert(requestId, clientMessageId);
  SetServiceActionStatus(tr("正在重试消息"));
}

void AppController::togglePinned(int index) {
  conversations_.TogglePinned(index);
}

void AppController::toggleMuted(int index) {
  conversations_.ToggleMuted(index);
}

void AppController::markRead(int index) {
  const auto *record = conversations_.RecordAt(index);
  if (record == nullptr) {
    return;
  }
  const QString conversationId = record->conversationId;
  const bool changed = conversations_.MarkRead(index);
  for (auto &conversation : snapshot_.conversations) {
    if (conversation.conversationId != conversationId) {
      continue;
    }
    conversation.unreadCount = 0;
    repository_->SaveConversation(conversation);
    break;
  }
  AcknowledgeConversationRead(index);
  if (changed && index == selected_conversation_index_) {
    emit selectedConversationChanged();
  }
}

qreal AppController::conversationListPosition() const {
  return conversation_list_position_;
}

void AppController::saveConversationListPosition(qreal position) {
  if (position >= 0) {
    conversation_list_position_ = position;
  }
}

qreal AppController::timelinePosition() const {
  return timeline_positions_.value(CurrentStateKey());
}

void AppController::saveTimelinePosition(qreal position) {
  const QString key = CurrentStateKey();
  if (!key.isEmpty() && position >= 0) {
    timeline_positions_[key] = position;
  }
}

void AppController::selectContact(int index) {
  if (contacts_.RecordAt(index) == nullptr) {
    return;
  }
  selected_contact_index_ = index;
  emit selectedContactChanged();
}

void AppController::toggleContactFavorite(int index) {
  if (contacts_.ToggleFavorite(index) && index == selected_contact_index_) {
    emit selectedContactChanged();
  }
}

void AppController::resolveRequest(int index, bool accepted) {
  const auto *request = requests_.RecordAt(index);
  if (request == nullptr || request->status != QStringLiteral("pending")) {
    return;
  }
  if (!network_enabled_) {
    SetRequestStatus(
        index,
        accepted ? QStringLiteral("accepted") : QStringLiteral("declined"),
        true);
    return;
  }
  if (!gateway_client_.IsReady()) {
    SetServiceActionStatus(tr("连接尚未就绪，暂时不能处理申请"));
    return;
  }

  QString requestId;
  if (request->kind == QStringLiteral("group")) {
    const QStringList fields = request->requestId.split(QLatin1Char(':'));
    if (fields.size() != 3) {
      SetServiceActionStatus(tr("群申请缺少必要标识"));
      return;
    }
    bool groupValid = false;
    bool userValid = false;
    const auto groupId = fields[1].toLongLong(&groupValid);
    const auto userId = fields[2].toLongLong(&userValid);
    if (!groupValid || !userValid) {
      SetServiceActionStatus(tr("群申请标识无效"));
      return;
    }
    requestId = gateway_client_.ReplyJoinGroup(groupId, userId, accepted);
  } else {
    bool valid = false;
    const auto userId = request->requestId.toLongLong(&valid);
    if (!valid || userId <= 0) {
      SetServiceActionStatus(tr("好友申请缺少用户 ID"));
      return;
    }
    requestId = gateway_client_.ReplyFriendRequest(userId, accepted, {});
  }
  friend_reply_requests_.insert(requestId, accepted ? index + 1 : -(index + 1));
  SetRequestStatus(
      index,
      accepted ? QStringLiteral("accepting") : QStringLiteral("declining"),
      false);
  SetServiceActionStatus(tr("正在处理申请"));
}

void AppController::authenticate(const QString &username,
                                 const QString &password) {
  if (!network_enabled_) {
    completeAuthentication();
    return;
  }
  if (authentication_busy_) {
    return;
  }
  if (username.trimmed().isEmpty() || password.isEmpty()) {
    authentication_error_ = tr("请输入账号和密码");
    emit authenticationStateChanged();
    return;
  }
  authentication_busy_ = true;
  authentication_error_.clear();
  emit authenticationStateChanged();
  SetConnectionStatus(QStringLiteral("connecting"));
  gate_client_.SignIn(username.trimmed(), password);
}

void AppController::requestVerificationCode(const QString &email) {
  if (!network_enabled_ || authentication_busy_) {
    return;
  }
  if (email.trimmed().isEmpty()) {
    authentication_error_ = tr("请输入邮箱");
    emit authenticationStateChanged();
    return;
  }
  authentication_busy_ = true;
  authentication_error_.clear();
  emit authenticationStateChanged();
  gate_client_.RequestVerificationCode(email.trimmed());
}

void AppController::registerAccount(const QString &username,
                                    const QString &password,
                                    const QString &email,
                                    const QString &verificationCode) {
  if (!network_enabled_ || authentication_busy_) {
    return;
  }
  if (username.trimmed().isEmpty() || password.isEmpty() ||
      email.trimmed().isEmpty() || verificationCode.trimmed().isEmpty()) {
    authentication_error_ = tr("请完整填写账号、密码、邮箱和验证码");
    emit authenticationStateChanged();
    return;
  }
  authentication_busy_ = true;
  authentication_error_.clear();
  emit authenticationStateChanged();
  gate_client_.SignUp(username.trimmed(), password, email.trimmed(),
                      verificationCode.trimmed());
}

void AppController::resetPassword(const QString &username, const QString &email,
                                  const QString &verificationCode,
                                  const QString &newPassword) {
  if (!network_enabled_ || authentication_busy_) {
    return;
  }
  if (username.trimmed().isEmpty() || email.trimmed().isEmpty() ||
      verificationCode.trimmed().isEmpty() || newPassword.isEmpty()) {
    authentication_error_ = tr("请完整填写账号、邮箱、验证码和新密码");
    emit authenticationStateChanged();
    return;
  }
  authentication_busy_ = true;
  authentication_error_.clear();
  emit authenticationStateChanged();
  gate_client_.ForgetPassword(username.trimmed(), email.trimmed(),
                              verificationCode.trimmed(), newPassword);
}

void AppController::startConversationWithSelectedContact() {
  const auto *contact = contacts_.RecordAt(selected_contact_index_);
  if (contact == nullptr) {
    return;
  }
  bool valid = false;
  const auto peerUid = contact->userId.toLongLong(&valid);
  if (!valid || peerUid <= 0) {
    if (network_enabled_) {
      SetServiceActionStatus(tr("联系人缺少服务端用户 ID"));
      return;
    }
    setCurrentSection(QStringLiteral("chats"));
    return;
  }
  const QString conversationId = EnsureDirectConversation(
      peerUid, contact->displayName, contact->avatarColor);
  setCurrentSection(QStringLiteral("chats"));
  selectConversation(conversations_.IndexOf(conversationId));
}

void AppController::sendFriendRequest(const QString &uid,
                                      const QString &message) {
  bool valid = false;
  const auto recipientUid = uid.toLongLong(&valid);
  if (!valid || recipientUid <= 0) {
    SetServiceActionStatus(tr("请输入有效用户 ID"));
    return;
  }
  if (!network_enabled_) {
    SetServiceActionStatus(
        message.trimmed().isEmpty()
            ? tr("演示：已向用户 %1 发送好友申请").arg(recipientUid)
            : tr("演示：已向用户 %1 发送好友申请和验证消息").arg(recipientUid));
    return;
  }
  if (!gateway_client_.IsReady()) {
    SetServiceActionStatus(tr("连接尚未就绪，暂时不能发送好友申请"));
    return;
  }
  gateway_client_.SendFriendRequest(recipientUid, message.trimmed());
  SetServiceActionStatus(tr("好友申请已提交"));
}

void AppController::createGroup(const QString &name) {
  const QString trimmedName = name.trimmed();
  if (trimmedName.isEmpty()) {
    SetServiceActionStatus(tr("群名不能为空"));
    return;
  }
  if (!network_enabled_) {
    const QString conversationId =
        EnsureGroupConversation(++next_fake_group_id_, trimmedName);
    setCurrentSection(QStringLiteral("chats"));
    selectConversation(conversations_.IndexOf(conversationId));
    SetServiceActionStatus(tr("演示群聊已创建"));
    return;
  }
  if (!gateway_client_.IsReady()) {
    SetServiceActionStatus(tr("连接尚未就绪，暂时不能创建群聊"));
    return;
  }
  const QString requestId = gateway_client_.CreateGroup(trimmedName);
  create_group_requests_.insert(requestId, trimmedName);
  SetServiceActionStatus(tr("创建群请求已提交"));
}

void AppController::joinGroup(const QString &groupId, const QString &message) {
  bool valid = false;
  const auto numericGroupId = groupId.toLongLong(&valid);
  if (!valid || numericGroupId <= 0) {
    SetServiceActionStatus(tr("请输入有效群 ID"));
    return;
  }
  if (!network_enabled_) {
    SetServiceActionStatus(
        message.trimmed().isEmpty()
            ? tr("演示：已申请加入群 %1").arg(numericGroupId)
            : tr("演示：已提交群 %1 的入群申请和说明").arg(numericGroupId));
    return;
  }
  if (!gateway_client_.IsReady()) {
    SetServiceActionStatus(tr("连接尚未就绪，暂时不能申请入群"));
    return;
  }
  gateway_client_.RequestJoinGroup(numericGroupId, message.trimmed());
  SetServiceActionStatus(tr("入群申请已提交"));
}

void AppController::completeAuthentication() {
  if (!authRequired()) {
    return;
  }
  authentication_completed_ = true;
  SetConnectionStatus(QStringLiteral("online"));
  emit authRequiredChanged();
}

void AppController::saveGateUrl(const QString &gateUrl) {
  QString normalized = gateUrl.trimmed();
  if (!normalized.isEmpty() && !normalized.contains(QStringLiteral("://"))) {
    normalized.prepend(QStringLiteral("http://"));
  }

  const QUrl parsed(normalized);
  if (!normalized.isEmpty() && (!parsed.isValid() || parsed.host().isEmpty() ||
                                (parsed.scheme() != QStringLiteral("http") &&
                                 parsed.scheme() != QStringLiteral("https")))) {
    gate_configuration_status_ = QStringLiteral("invalid");
    emit gateConfigurationChanged();
    return;
  }

  while (normalized.endsWith(QLatin1Char('/'))) {
    normalized.chop(1);
  }

  QSettings settings;
  if (normalized.isEmpty()) {
    settings.remove(QStringLiteral("network/gateUrl"));
  } else {
    settings.setValue(QStringLiteral("network/gateUrl"), normalized);
  }
  settings.sync();

  const QString activeUrl = gate_client_.BaseUrl().toString();
  const bool restartRequired =
      network_enabled_ ? activeUrl != normalized : !normalized.isEmpty();
  gate_url_ = normalized;
  gate_configuration_status_ = restartRequired
                                   ? QStringLiteral("restart-required")
                                   : QStringLiteral("saved");
  emit gateConfigurationChanged();
}

void AppController::sendTestDesktopNotification() {
  const QString nextStatus =
      platform_services_->ShowDesktopNotification(
          tr("WIMI 桌面通知"), tr("Linux 平台通知端口工作正常。"))
          ? QStringLiteral("sent")
          : QStringLiteral("unavailable");
  if (notification_test_status_ == nextStatus) {
    return;
  }
  notification_test_status_ = nextStatus;
  emit notificationTestStatusChanged();
}

void AppController::ConfigureNetwork(const QString &gateUrl) {
  gate_client_.SetBaseUrl(QUrl(gateUrl));

  connect(&gate_client_, &GateHttpClient::SignInSucceeded, this,
          [this](const GateSession &session) {
            authenticated_uid_ = session.uid;
            SetConnectionStatus(QStringLiteral("connecting"));
            gateway_client_.Open(session);
          });
  connect(&gate_client_, &GateHttpClient::OperationFailed, this,
          [this](const QString &operation, int, const QString &message) {
            authentication_busy_ = false;
            authentication_error_ = message;
            if (operation == QStringLiteral("sign-in")) {
              SetConnectionStatus(QStringLiteral("auth-expired"));
            }
            emit authenticationStateChanged();
          });
  connect(&gate_client_, &GateHttpClient::OperationSucceeded, this,
          [this](const QString &operation, const QJsonObject &) {
            if (operation == QStringLiteral("sign-in")) {
              return;
            }
            authentication_busy_ = false;
            authentication_error_.clear();
            emit authenticationStateChanged();
            const QString message = operation == QStringLiteral("verify-code")
                                        ? tr("验证码已发送")
                                    : operation == QStringLiteral("sign-up")
                                        ? tr("注册成功，请登录")
                                        : tr("密码已更新，请重新登录");
            emit authenticationOperationSucceeded(operation, message);
          });

  connect(&gateway_client_, &ConnectionGatewayClient::Authenticated, this,
          [this] {
            authentication_busy_ = false;
            authentication_error_.clear();
            authentication_completed_ = true;
            SetConnectionStatus(QStringLiteral("online"));
            emit authenticationStateChanged();
            emit authRequiredChanged();
            gateway_client_.PullFriendList();
            gateway_client_.PullFriendApplications();
            SyncKnownConversations();
            ResumePendingOutgoing();
          });
  connect(&gateway_client_, &ConnectionGatewayClient::CredentialsExpired, this,
          [this] {
            authentication_completed_ = false;
            authentication_busy_ = false;
            authentication_error_ = tr("登录凭证已过期，请重新登录");
            SetConnectionStatus(QStringLiteral("auth-expired"));
            emit authenticationStateChanged();
            emit authRequiredChanged();
          });
  connect(&gateway_client_, &ConnectionGatewayClient::StateChanged, this,
          [this](ConnectionGatewayClient::State state) {
            if (state == ConnectionGatewayClient::State::Connecting ||
                state == ConnectionGatewayClient::State::Authenticating) {
              SetConnectionStatus(QStringLiteral("connecting"));
            } else if (state == ConnectionGatewayClient::State::Reconnecting) {
              SetConnectionStatus(QStringLiteral("reconnecting"));
            } else if (state == ConnectionGatewayClient::State::Disconnected &&
                       authentication_completed_) {
              SetConnectionStatus(QStringLiteral("offline-cached"));
            }
          });
  connect(&gateway_client_, &ConnectionGatewayClient::ResponseReceived, this,
          &AppController::HandleGatewayResponse);
  connect(&gateway_client_, &ConnectionGatewayClient::PushReceived, this,
          &AppController::HandleGatewayPush);
  connect(&gateway_client_, &ConnectionGatewayClient::RequestFailed, this,
          &AppController::HandleGatewayFailure);
  connect(&gateway_client_, &ConnectionGatewayClient::ProtocolError, this,
          [this](const QString &message) { SetServiceActionStatus(message); });

  authentication_completed_ = false;
  SetConnectionStatus(QStringLiteral("auth-required"));
  emit authRequiredChanged();
}

void AppController::HandleGatewayResponse(const QString &requestId,
                                          quint32 serviceId,
                                          const QByteArray &payload) {
  wimi::protocol::Packet response;
  if (!ParseProtobufPacket(payload, &response)) {
    SetServiceActionStatus(tr("服务端响应无法解析"));
    return;
  }
  const int errorCode =
      response.hasError() ? static_cast<int>(response.error()) : 0;

  if (serviceId == protocol::CreateGroupResponse) {
    const QString groupName = create_group_requests_.take(requestId);
    if (errorCode != protocol::Success || !response.hasGroupId()) {
      SetServiceActionStatus(response.hasMessage()
                                 ? response.message()
                                 : tr("创建群失败（%1）").arg(errorCode));
      return;
    }
    const QString localConversationId = EnsureGroupConversation(
        response.groupId(),
        groupName.isEmpty()
            ? tr("群聊 %1").arg(static_cast<qint64>(response.groupId()))
            : groupName);
    if (response.hasConversationId()) {
      AttachRemoteConversation(localConversationId, response.conversationId());
    }
    setCurrentSection(QStringLiteral("chats"));
    selectConversation(conversations_.IndexOf(localConversationId));
    SetServiceActionStatus(tr("群聊已创建，群 ID：%1")
                               .arg(static_cast<qint64>(response.groupId())));
    return;
  }

  if (serviceId == protocol::SendTextResponse ||
      serviceId == protocol::SendGroupTextResponse) {
    const auto found = outgoing_request_messages_.find(requestId);
    if (found == outgoing_request_messages_.end()) {
      return;
    }
    const std::int64_t clientMessageId = found.value();
    outgoing_request_messages_.erase(found);
    if (errorCode != protocol::Success || !response.hasMessageId() ||
        !response.hasConversationId() || !response.hasConversationSeq()) {
      UpdateMessageState(
          clientMessageId,
          response.retryable() || protocol::IsRetryableError(errorCode)
              ? MessageDeliveryState::RetryableFailed
              : MessageDeliveryState::PermanentFailed);
      SetServiceActionStatus(
          response.hasMessage()
              ? response.message()
              : tr("消息发送被服务端拒绝（%1）").arg(errorCode));
      return;
    }

    QString localConversationId;
    for (auto iterator = snapshot_.messagesByConversation.begin();
         iterator != snapshot_.messagesByConversation.end(); ++iterator) {
      for (auto &message : iterator.value()) {
        if (message.clientMessageId != clientMessageId) {
          continue;
        }
        message.messageId = response.messageId();
        message.conversationSeq = response.conversationSeq();
        message.deliveryState = MessageDeliveryState::Accepted;
        localConversationId = iterator.key();
        break;
      }
      if (!localConversationId.isEmpty()) {
        break;
      }
    }
    if (localConversationId.isEmpty()) {
      return;
    }
    if (!repository_->AcceptOutgoing(clientMessageId, response.messageId(),
                                     localConversationId,
                                     response.conversationSeq())) {
      SetServiceActionStatus(tr("发送消息写入本地数据库失败"));
      return;
    }
    messages_.UpdateDeliveryState(clientMessageId,
                                  MessageDeliveryState::Accepted);
    AttachRemoteConversation(localConversationId, response.conversationId());
    const std::int64_t cursor = repository_->SyncCursor(localConversationId);
    if (response.conversationSeq() == cursor + 1 &&
        repository_->ApplyIncomingBatch(localConversationId, {},
                                        response.conversationSeq())) {
      gateway_client_.AcknowledgeDelivered(response.messageId(),
                                           response.conversationId(),
                                           response.conversationSeq());
      DrainPendingPush(localConversationId);
    } else if (response.conversationSeq() > cursor + 1) {
      const QString syncRequest = gateway_client_.PullConversationMessages(
          response.conversationId(), cursor);
      sync_request_conversations_.insert(syncRequest, localConversationId);
      gap_syncing_.insert(localConversationId);
    }
    return;
  }

  if (serviceId == protocol::PullFriendListResponse) {
    if (errorCode != protocol::Success) {
      SetServiceActionStatus(tr("拉取好友列表失败（%1）").arg(errorCode));
      return;
    }
    QVector<ContactRecord> contacts;
    contacts.reserve(response.friendList().size());
    static const QStringList colors = {
        QStringLiteral("#315FD6"), QStringLiteral("#7656A8"),
        QStringLiteral("#247B6B"), QStringLiteral("#A05A4E")};
    for (const auto &friendInfo : response.friendList()) {
      const QString uid = QString::number(friendInfo.uid());
      bool favorite = false;
      for (const auto &existing : snapshot_.contacts) {
        if (existing.userId == uid) {
          favorite = existing.favorite;
          break;
        }
      }
      contacts.push_back(ContactRecord{
          .userId = uid,
          .displayName = friendInfo.name().isEmpty() ? tr("用户 %1").arg(uid)
                                                     : friendInfo.name(),
          .statusText = tr("WIMI 用户 · %1").arg(uid),
          .avatarColor = colors[friendInfo.uid() % colors.size()],
          .online = false,
          .favorite = favorite,
      });
    }
    snapshot_.contacts = contacts;
    contacts_.SetRecords(contacts);
    selected_contact_index_ = contacts.isEmpty() ? -1 : 0;
    repository_->ReplaceContacts(contacts);
    emit selectedContactChanged();
    return;
  }

  if (serviceId == protocol::PullFriendApplyListResponse) {
    if (errorCode != protocol::Success) {
      SetServiceActionStatus(tr("拉取好友申请失败（%1）").arg(errorCode));
      return;
    }
    QVector<RequestRecord> requests;
    requests.reserve(response.applyList().size());
    for (const auto &apply : response.applyList()) {
      const QString from = QString::number(apply.from());
      const QString status = apply.status() == 0   ? QStringLiteral("pending")
                             : apply.status() == 1 ? QStringLiteral("accepted")
                                                   : QStringLiteral("declined");
      requests.push_back(RequestRecord{
          .requestId = from,
          .displayName = tr("用户 %1").arg(from),
          .message = apply.content(),
          .avatarColor = QStringLiteral("#367A91"),
          .kind = QStringLiteral("friend"),
          .status = status,
      });
    }
    snapshot_.requests = requests;
    requests_.SetRecords(requests);
    repository_->ReplaceRequests(requests);
    return;
  }

  if (serviceId == protocol::PullSessionMessagesResponse) {
    const QString localConversationId =
        sync_request_conversations_.take(requestId);
    if (localConversationId.isEmpty() || errorCode != protocol::Success) {
      if (!localConversationId.isEmpty()) {
        gap_syncing_.remove(localConversationId);
        ScheduleGapSync(localConversationId);
      }
      if (errorCode != protocol::Success) {
        SetServiceActionStatus(tr("同步会话失败（%1）").arg(errorCode));
      }
      return;
    }
    AttachRemoteConversation(localConversationId, response.conversationId());
    auto &stored = snapshot_.messagesByConversation[localConversationId];
    QVector<MessageRecord> additions;
    for (const auto &item : response.messageList()) {
      const auto duplicate =
          std::find_if(stored.cbegin(), stored.cend(),
                       [&item](const MessageRecord &message) {
                         return message.messageId == item.messageId();
                       });
      if (duplicate != stored.cend()) {
        if (item.from() == authenticated_uid_) {
          UpdateMessageState(duplicate->clientMessageId,
                             item.status() >= 3 ? MessageDeliveryState::Read
                             : item.status() >= 2
                                 ? MessageDeliveryState::Delivered
                                 : MessageDeliveryState::Accepted);
        }
        continue;
      }
      additions.push_back(MessageRecord{
          .clientMessageId = item.messageId(),
          .messageId = item.messageId(),
          .conversationSeq = item.conversationSeq(),
          .senderId = QString::number(item.from()),
          .body = item.content(),
          .timestamp = item.sendDateTime(),
          .outgoing = item.from() == authenticated_uid_,
          .deliveryState = item.status() >= 3 ? MessageDeliveryState::Read
                           : item.status() >= 2
                               ? MessageDeliveryState::Delivered
                               : MessageDeliveryState::Accepted,
      });
    }
    const std::int64_t nextCursor =
        response.hasNextSeq() ? static_cast<std::int64_t>(response.nextSeq())
                              : repository_->SyncCursor(localConversationId);
    if (!repository_->ApplyIncomingBatch(localConversationId, additions,
                                         nextCursor)) {
      SetServiceActionStatus(tr("同步消息写入本地数据库失败"));
      return;
    }
    for (const auto &message : additions) {
      stored.push_back(message);
    }
    std::stable_sort(stored.begin(), stored.end(),
                     [](const MessageRecord &left, const MessageRecord &right) {
                       if (!left.conversationSeq.has_value())
                         return false;
                       if (!right.conversationSeq.has_value())
                         return true;
                       return *left.conversationSeq < *right.conversationSeq;
                     });
    for (const auto &item : response.messageList()) {
      gateway_client_.AcknowledgeDelivered(
          item.messageId(), response.conversationId(), item.conversationSeq());
    }
    if (CurrentConversationId() == localConversationId) {
      messages_.SetRecords(stored);
      AcknowledgeConversationRead(selected_conversation_index_);
    }
    if (response.hasMore() && response.hasNextSeq()) {
      const QString nextRequest = gateway_client_.PullConversationMessages(
          response.conversationId(), response.nextSeq());
      sync_request_conversations_.insert(nextRequest, localConversationId);
    } else {
      gap_syncing_.remove(localConversationId);
      DrainPendingPush(localConversationId);
    }
    return;
  }

  const auto reply = friend_reply_requests_.find(requestId);
  if (reply != friend_reply_requests_.end()) {
    const int encodedIndex = reply.value();
    friend_reply_requests_.erase(reply);
    if (errorCode == protocol::Success) {
      const int index = std::abs(encodedIndex) - 1;
      SetRequestStatus(index,
                       encodedIndex > 0 ? QStringLiteral("accepted")
                                        : QStringLiteral("declined"),
                       true);
      gateway_client_.PullFriendList();
      SetServiceActionStatus(tr("申请已处理"));
    } else {
      SetRequestStatus(std::abs(encodedIndex) - 1, QStringLiteral("pending"),
                       false);
      SetServiceActionStatus(tr("处理申请失败（%1）").arg(errorCode));
    }
    return;
  }

  SetServiceActionStatus(errorCode == protocol::Success
                             ? tr("服务请求已完成")
                             : tr("服务请求失败（%1）").arg(errorCode));
}

void AppController::ScheduleGapSync(const QString &localConversationId) {
  QTimer *timer = gap_timers_.value(localConversationId);
  if (timer == nullptr) {
    timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(200);
    gap_timers_.insert(localConversationId, timer);
    connect(timer, &QTimer::timeout, this, [this, localConversationId] {
      auto pending = pending_pushes_.find(localConversationId);
      if (pending == pending_pushes_.end() || pending->isEmpty() ||
          gap_syncing_.contains(localConversationId)) {
        return;
      }
      const std::int64_t cursor = repository_->SyncCursor(localConversationId);
      if (pending->firstKey() <= cursor + 1) {
        DrainPendingPush(localConversationId);
        return;
      }
      const auto conversation = std::find_if(
          snapshot_.conversations.cbegin(), snapshot_.conversations.cend(),
          [&localConversationId](const ConversationRecord &candidate) {
            return candidate.conversationId == localConversationId;
          });
      if (conversation == snapshot_.conversations.cend() ||
          !conversation->remoteConversationId.has_value()) {
        return;
      }
      const QString requestId = gateway_client_.PullConversationMessages(
          *conversation->remoteConversationId, cursor);
      sync_request_conversations_.insert(requestId, localConversationId);
      gap_syncing_.insert(localConversationId);
    });
  }
  if (!timer->isActive())
    timer->start();
}

void AppController::DrainPendingPush(const QString &localConversationId) {
  auto pending = pending_pushes_.find(localConversationId);
  if (pending == pending_pushes_.end() || pending->isEmpty())
    return;
  const std::int64_t cursor = repository_->SyncCursor(localConversationId);
  while (!pending->isEmpty() && pending->firstKey() <= cursor) {
    pending->erase(pending->begin());
  }
  if (pending->isEmpty()) {
    pending_pushes_.erase(pending);
    return;
  }
  const std::int64_t expected = cursor + 1;
  auto next = pending->find(expected);
  if (next == pending->end()) {
    ScheduleGapSync(localConversationId);
    return;
  }
  const auto frame = next.value();
  pending->erase(next);
  if (pending->isEmpty())
    pending_pushes_.erase(pending);
  QTimer::singleShot(
      0, this, [this, frame] { HandleGatewayPush(frame.first, frame.second); });
}

void AppController::HandleGatewayPush(quint32 serviceId,
                                      const QByteArray &payload) {
  wimi::protocol::Packet packet;
  if (!ParseProtobufPacket(payload, &packet)) {
    SetServiceActionStatus(tr("收到无法解析的推送"));
    return;
  }

  if (serviceId == protocol::TextReadReceiptNotification) {
    if (!packet.hasMessageId() || !packet.hasSeq()) {
      SetServiceActionStatus(tr("收到不完整的已读通知"));
      return;
    }
    const auto messageId = static_cast<std::int64_t>(packet.messageId());
    for (const auto &stored : snapshot_.messagesByConversation) {
      const auto message =
          std::find_if(stored.cbegin(), stored.cend(),
                       [messageId](const MessageRecord &item) {
                         return item.outgoing && item.messageId == messageId;
                       });
      if (message == stored.cend()) {
        continue;
      }
      UpdateMessageState(message->clientMessageId, MessageDeliveryState::Read);
      break;
    }
    gateway_client_.AcknowledgeDelivered(packet.seq(), 0, 0);
    return;
  }

  if (serviceId == protocol::SendTextRequest ||
      serviceId == protocol::SendGroupTextRequest) {
    const bool group = serviceId == protocol::SendGroupTextRequest;
    const std::int64_t directPeer =
        packet.from() == authenticated_uid_ ? packet.to() : packet.from();
    const QString localConversationId =
        group ? GroupConversationId(packet.groupId())
              : EnsureDirectConversation(
                    directPeer,
                    tr("用户 %1").arg(static_cast<qint64>(directPeer)),
                    QStringLiteral("#315FD6"));
    if (group && conversations_.IndexOf(localConversationId) < 0) {
      EnsureGroupConversation(
          packet.groupId(),
          tr("群聊 %1").arg(static_cast<qint64>(packet.groupId())));
    }
    if (packet.hasConversationId()) {
      AttachRemoteConversation(localConversationId, packet.conversationId());
    }
    if (!packet.hasConversationId() || !packet.hasConversationSeq()) {
      SetServiceActionStatus(tr("收到缺少会话序号的消息"));
      return;
    }
    const std::int64_t messageId =
        packet.hasMessageId() ? packet.messageId() : packet.seq();
    const std::int64_t conversationSeq = packet.conversationSeq();
    const std::int64_t cursor = repository_->SyncCursor(localConversationId);
    if (conversationSeq > cursor + 1) {
      pending_pushes_[localConversationId].insert(
          conversationSeq, qMakePair(serviceId, payload));
      ScheduleGapSync(localConversationId);
      return;
    }
    if (conversationSeq <= cursor) {
      gateway_client_.AcknowledgeDelivered(messageId, packet.conversationId(),
                                           packet.conversationSeq());
      DrainPendingPush(localConversationId);
      return;
    }
    auto &stored = snapshot_.messagesByConversation[localConversationId];
    const bool duplicate =
        std::any_of(stored.cbegin(), stored.cend(),
                    [messageId](const MessageRecord &message) {
                      return message.messageId == messageId;
                    });
    if (duplicate) {
      if (!repository_->ApplyIncomingBatch(localConversationId, {},
                                           conversationSeq)) {
        SetServiceActionStatus(tr("消息游标写入本地数据库失败"));
        return;
      }
      gateway_client_.AcknowledgeDelivered(
          messageId,
          packet.hasConversationId()
              ? static_cast<std::int64_t>(packet.conversationId())
              : 0,
          packet.hasConversationSeq()
              ? static_cast<std::int64_t>(packet.conversationSeq())
              : 0);
      DrainPendingPush(localConversationId);
      return;
    }
    MessageRecord incoming{
        .clientMessageId = messageId,
        .messageId = messageId,
        .conversationSeq =
            packet.hasConversationSeq()
                ? std::optional<std::int64_t>(packet.conversationSeq())
                : std::nullopt,
        .senderId = QString::number(packet.from()),
        .body = QString::fromUtf8(packet.data()),
        .timestamp = PacketSendDateTimeOrEmpty(packet).isEmpty()
                         ? DisplayTimestamp(QString{})
                         : PacketSendDateTimeOrEmpty(packet),
        .outgoing = packet.from() == authenticated_uid_,
        .deliveryState = MessageDeliveryState::Delivered,
    };
    const std::int64_t persistedCursor = incoming.conversationSeq.value_or(
        repository_->SyncCursor(localConversationId));
    if (!repository_->ApplyIncomingBatch(localConversationId, {incoming},
                                         persistedCursor)) {
      SetServiceActionStatus(tr("推送消息写入本地数据库失败"));
      return;
    }
    stored.push_back(incoming);
    if (CurrentConversationId() == localConversationId) {
      messages_.Append(incoming);
    }
    const QString previewTimestamp = DisplayTimestamp(incoming.timestamp);
    conversations_.UpdatePreview(localConversationId, incoming.body,
                                 previewTimestamp);
    for (auto &conversation : snapshot_.conversations) {
      if (conversation.conversationId != localConversationId) {
        continue;
      }
      conversation.preview = incoming.body;
      conversation.timestamp = previewTimestamp;
      break;
    }
    if (packet.hasConversationId() && packet.hasConversationSeq()) {
      gateway_client_.AcknowledgeDelivered(messageId, packet.conversationId(),
                                           packet.conversationSeq());
      if (CurrentConversationId() == localConversationId) {
        gateway_client_.AcknowledgeRead(messageId, packet.conversationId(),
                                        packet.conversationSeq());
      } else {
        for (auto &conversation : snapshot_.conversations) {
          if (conversation.conversationId != localConversationId) {
            continue;
          }
          ++conversation.unreadCount;
          conversations_.SetUnreadCount(localConversationId,
                                        conversation.unreadCount);
          repository_->SaveConversation(conversation);
          break;
        }
      }
    }
    DrainPendingPush(localConversationId);
    return;
  }

  if (serviceId == protocol::AddFriendRequest ||
      serviceId == protocol::JoinGroupRequest) {
    RequestRecord request{
        .requestId = serviceId == protocol::JoinGroupRequest
                         ? QStringLiteral("group:%1:%2")
                               .arg(static_cast<qint64>(packet.groupId()))
                               .arg(static_cast<qint64>(packet.uid()))
                         : QString::number(packet.from()),
        .displayName = tr("用户 %1").arg(static_cast<qint64>(
            serviceId == protocol::JoinGroupRequest ? packet.uid()
                                                    : packet.from())),
        .message = serviceId == protocol::JoinGroupRequest
                       ? packet.content()
                       : packet.requestMessage(),
        .avatarColor = QStringLiteral("#367A91"),
        .kind = serviceId == protocol::JoinGroupRequest
                    ? QStringLiteral("group")
                    : QStringLiteral("friend"),
        .status = QStringLiteral("pending"),
    };
    snapshot_.requests.push_back(std::move(request));
    requests_.SetRecords(snapshot_.requests);
    repository_->ReplaceRequests(snapshot_.requests);
    if (packet.hasSeq()) {
      gateway_client_.AcknowledgeDelivered(packet.seq(), 0, 0);
    }
    return;
  }

  if (serviceId == protocol::ReplyFriendRequest ||
      serviceId == protocol::ReplyJoinGroupRequest) {
    if (packet.hasSeq()) {
      gateway_client_.AcknowledgeDelivered(packet.seq(), 0, 0);
    }
    gateway_client_.PullFriendList();
    gateway_client_.PullFriendApplications();
    if (serviceId == protocol::ReplyJoinGroupRequest && packet.accept() &&
        packet.requestorUid() == authenticated_uid_ && packet.groupId() > 0) {
      EnsureGroupConversation(
          packet.groupId(),
          tr("群聊 %1").arg(static_cast<qint64>(packet.groupId())));
    }
  }
}

void AppController::HandleGatewayFailure(const QString &requestId,
                                         quint32 serviceId, int errorCode,
                                         const QString &message,
                                         bool outcomeUnknown) {
  const auto outgoing = outgoing_request_messages_.find(requestId);
  if (outgoing != outgoing_request_messages_.end()) {
    const auto clientMessageId = outgoing.value();
    outgoing_request_messages_.erase(outgoing);
    UpdateMessageState(clientMessageId,
                       outcomeUnknown ? MessageDeliveryState::Unknown
                       : protocol::IsRetryableError(errorCode)
                           ? MessageDeliveryState::RetryableFailed
                           : MessageDeliveryState::PermanentFailed);
  }
  const QString syncConversation = sync_request_conversations_.take(requestId);
  if (!syncConversation.isEmpty()) {
    gap_syncing_.remove(syncConversation);
    ScheduleGapSync(syncConversation);
  }
  const int encodedRequestIndex = friend_reply_requests_.take(requestId);
  if (encodedRequestIndex != 0) {
    SetRequestStatus(std::abs(encodedRequestIndex) - 1,
                     QStringLiteral("pending"), false);
  }
  create_group_requests_.remove(requestId);
  SetServiceActionStatus(tr("请求 %1 失败：%2").arg(serviceId).arg(message));
}

void AppController::SyncKnownConversations() {
  for (const auto &conversation : snapshot_.conversations) {
    if (!conversation.remoteConversationId.has_value()) {
      continue;
    }
    const QString requestId = gateway_client_.PullConversationMessages(
        *conversation.remoteConversationId,
        repository_->SyncCursor(conversation.conversationId));
    sync_request_conversations_.insert(requestId, conversation.conversationId);
  }
}

void AppController::ResumePendingOutgoing() {
  for (auto iterator = snapshot_.messagesByConversation.begin();
       iterator != snapshot_.messagesByConversation.end(); ++iterator) {
    const auto peerUid = DirectPeerUid(iterator.key());
    const auto groupId = GroupId(iterator.key());
    if (!peerUid.has_value() && !groupId.has_value()) {
      continue;
    }
    const int conversationIndex = conversations_.IndexOf(iterator.key());
    const auto *conversation = conversations_.RecordAt(conversationIndex);
    const std::int64_t remoteConversationId =
        conversation == nullptr
            ? 0
            : conversation->remoteConversationId.value_or(0);
    for (auto &message : iterator.value()) {
      if (!message.outgoing ||
          (message.deliveryState != MessageDeliveryState::PendingLocal &&
           message.deliveryState != MessageDeliveryState::WaitingAccept &&
           message.deliveryState != MessageDeliveryState::Unknown &&
           message.deliveryState != MessageDeliveryState::RetryableFailed)) {
        continue;
      }
      if (message.deliveryState != MessageDeliveryState::WaitingAccept) {
        UpdateMessageState(message.clientMessageId,
                           MessageDeliveryState::WaitingAccept);
      }
      const QString wireClientMessageId = QStringLiteral("%1:%2")
                                              .arg(authenticated_uid_)
                                              .arg(-message.clientMessageId);
      const QString requestId =
          peerUid.has_value()
              ? gateway_client_.SendText(*peerUid, message.body.toUtf8(),
                                         wireClientMessageId,
                                         remoteConversationId)
              : gateway_client_.SendGroupText(*groupId, message.body.toUtf8(),
                                              wireClientMessageId,
                                              remoteConversationId);
      outgoing_request_messages_.insert(requestId, message.clientMessageId);
    }
  }
}

void AppController::AcknowledgeConversationRead(int index) {
  if (!network_enabled_ || !gateway_client_.IsReady()) {
    return;
  }
  const auto *conversation = conversations_.RecordAt(index);
  if (conversation == nullptr ||
      !conversation->remoteConversationId.has_value()) {
    return;
  }
  const auto messages =
      snapshot_.messagesByConversation.value(conversation->conversationId);
  for (auto iterator = messages.crbegin(); iterator != messages.crend();
       ++iterator) {
    if (iterator->outgoing || !iterator->messageId.has_value() ||
        !iterator->conversationSeq.has_value()) {
      continue;
    }
    gateway_client_.AcknowledgeRead(*iterator->messageId,
                                    *conversation->remoteConversationId,
                                    *iterator->conversationSeq);
    return;
  }
}

void AppController::SetRequestStatus(int index, const QString &status,
                                     bool persist) {
  if (!requests_.SetStatus(index, status) || !persist || index < 0 ||
      index >= snapshot_.requests.size()) {
    return;
  }
  snapshot_.requests[index].status = status;
  repository_->ReplaceRequests(snapshot_.requests);
}

QString AppController::EnsureDirectConversation(std::int64_t peerUid,
                                                const QString &title,
                                                const QString &avatarColor) {
  const QString conversationId = DirectConversationId(peerUid);
  if (conversations_.IndexOf(conversationId) >= 0) {
    return conversationId;
  }
  ConversationRecord conversation{
      .conversationId = conversationId,
      .title = title,
      .preview = QString{},
      .timestamp = QString{},
      .avatarColor = avatarColor,
  };
  if (!repository_->SaveConversation(conversation)) {
    SetServiceActionStatus(tr("无法创建本地会话"));
    return {};
  }
  snapshot_.conversations.prepend(conversation);
  snapshot_.messagesByConversation.insert(conversationId, {});
  if (selected_conversation_index_ >= 0) {
    ++selected_conversation_index_;
  }
  conversations_.Prepend(std::move(conversation));
  return conversationId;
}

QString AppController::EnsureGroupConversation(std::int64_t groupId,
                                               const QString &title) {
  const QString conversationId = GroupConversationId(groupId);
  if (conversations_.IndexOf(conversationId) >= 0) {
    return conversationId;
  }
  ConversationRecord conversation{
      .conversationId = conversationId,
      .title = title,
      .preview = QString{},
      .timestamp = QString{},
      .avatarColor = QStringLiteral("#7656A8"),
  };
  if (!repository_->SaveConversation(conversation)) {
    SetServiceActionStatus(tr("无法创建本地群会话"));
    return {};
  }
  snapshot_.conversations.prepend(conversation);
  snapshot_.messagesByConversation.insert(conversationId, {});
  if (selected_conversation_index_ >= 0) {
    ++selected_conversation_index_;
  }
  conversations_.Prepend(std::move(conversation));
  return conversationId;
}

void AppController::AttachRemoteConversation(
    const QString &localConversationId, std::int64_t remoteConversationId) {
  if (remoteConversationId <= 0) {
    return;
  }
  for (auto &conversation : snapshot_.conversations) {
    if (conversation.conversationId != localConversationId) {
      continue;
    }
    if (conversation.remoteConversationId == remoteConversationId) {
      return;
    }
    conversation.remoteConversationId = remoteConversationId;
    conversations_.SetRemoteConversationId(localConversationId,
                                           remoteConversationId);
    repository_->SetRemoteConversationId(localConversationId,
                                         remoteConversationId);
    return;
  }
}

void AppController::SetConnectionStatus(const QString &status) {
  if (snapshot_.connectionStatus == status) {
    return;
  }
  snapshot_.connectionStatus = status;
  emit connectionStatusChanged();
}

void AppController::SetServiceActionStatus(const QString &status) {
  if (service_action_status_ == status) {
    return;
  }
  service_action_status_ = status;
  emit serviceActionStatusChanged();
}

void AppController::LoadScenario(const QString &scenarioName) {
  snapshot_ = repository_->LoadScenario(scenarioName);
  next_client_message_id_ = -1000;
  for (auto iterator = snapshot_.messagesByConversation.cbegin();
       iterator != snapshot_.messagesByConversation.cend(); ++iterator) {
    for (const auto &message : iterator.value()) {
      next_client_message_id_ =
          std::min(next_client_message_id_, message.clientMessageId - 1);
    }
  }
  conversations_.SetRecords(snapshot_.conversations);
  contacts_.SetRecords(snapshot_.contacts);
  requests_.SetRecords(snapshot_.requests);
  for (auto iterator = snapshot_.draftsByConversation.cbegin();
       iterator != snapshot_.draftsByConversation.cend(); ++iterator) {
    drafts_[snapshot_.scenarioName + QStringLiteral(":") + iterator.key()] =
        iterator.value();
  }
  selected_conversation_index_ = snapshot_.conversations.isEmpty() ? -1 : 0;
  selected_contact_index_ = snapshot_.contacts.isEmpty() ? -1 : 0;
  authentication_completed_ = false;

  if (selected_conversation_index_ >= 0) {
    const auto &conversation = snapshot_.conversations.front();
    messages_.SetRecords(
        snapshot_.messagesByConversation.value(conversation.conversationId,
                                               QVector<MessageRecord>{}),
        conversation.unreadCount);
  } else {
    messages_.SetRecords({}, 0);
  }

  emit scenarioNameChanged();
  emit connectionStatusChanged();
  emit selectedConversationChanged();
  emit selectedContactChanged();
  emit draftTextChanged();
  emit authRequiredChanged();
}

void AppController::ScheduleDeliveryLifecycle(std::int64_t clientMessageId) {
  struct Transition {
    int delay;
    MessageDeliveryState state;
  };
  const Transition transitions[] = {
      {350, MessageDeliveryState::WaitingAccept},
      {900, MessageDeliveryState::Accepted},
      {1500, MessageDeliveryState::Delivered},
      {2300, MessageDeliveryState::Read},
  };

  for (const auto &transition : transitions) {
    QTimer::singleShot(transition.delay, this,
                       [this, clientMessageId, state = transition.state] {
                         UpdateMessageState(clientMessageId, state);
                       });
  }
}

void AppController::UpdateMessageState(std::int64_t clientMessageId,
                                       MessageDeliveryState state) {
  bool changed = false;
  for (auto iterator = snapshot_.messagesByConversation.begin();
       iterator != snapshot_.messagesByConversation.end(); ++iterator) {
    for (auto &message : iterator.value()) {
      if (message.clientMessageId != clientMessageId ||
          !CanTransition(message.deliveryState, state)) {
        continue;
      }
      message.deliveryState = state;
      changed = true;
      break;
    }
    if (changed) {
      break;
    }
  }

  if (changed) {
    repository_->UpdateDeliveryState(clientMessageId, state);
    messages_.UpdateDeliveryState(clientMessageId, state);
  }
}

QString AppController::CurrentConversationId() const {
  const auto *record = conversations_.RecordAt(selected_conversation_index_);
  return record == nullptr ? QString{} : record->conversationId;
}

QString AppController::CurrentStateKey() const {
  const QString conversationId = CurrentConversationId();
  if (conversationId.isEmpty()) {
    return {};
  }
  return snapshot_.scenarioName + QStringLiteral(":") + conversationId;
}

QString AppController::ArgumentValue(const QString &name,
                                     const QString &fallback) {
  const QString prefix = QStringLiteral("--") + name + QStringLiteral("=");
  const auto arguments = QCoreApplication::arguments();
  for (const auto &argument : arguments) {
    if (argument.startsWith(prefix)) {
      return argument.mid(prefix.size());
    }
  }
  return fallback;
}

}  // namespace wimi::client
