#include "adapters/fake/FakeScenarioRepository.h"

namespace wimi::client
{
namespace
{

MessageRecord Message(std::int64_t clientId, std::int64_t messageId, std::int64_t sequence, QString sender,
                      QString body, QString timestamp, bool outgoing,
                      MessageDeliveryState state = MessageDeliveryState::Read)
{
    return MessageRecord{
        .clientMessageId = clientId,
        .messageId = messageId,
        .conversationSeq = sequence,
        .senderId = std::move(sender),
        .body = std::move(body),
        .timestamp = std::move(timestamp),
        .outgoing = outgoing,
        .deliveryState = state,
    };
}

} // namespace

QStringList FakeScenarioRepository::ScenarioNames() const
{
    return {
        QStringLiteral("normal"),          QStringLiteral("empty-account"),   QStringLiteral("offline-cached"),
        QStringLiteral("first-bootstrap"), QStringLiteral("dense-chat-list"), QStringLiteral("send-lifecycle"),
        QStringLiteral("send-unknown"),    QStringLiteral("sync-gap"),        QStringLiteral("auth-expired"),
        QStringLiteral("friend-requests"), QStringLiteral("group-admin"),     QStringLiteral("long-content"),
        QStringLiteral("large-history"),
    };
}

ClientSnapshot FakeScenarioRepository::LoadScenario(const QString& scenarioName) const
{
    if (scenarioName == QStringLiteral("empty-account")) {
        return ClientSnapshot{
            .scenarioName = scenarioName,
            .connectionStatus = QStringLiteral("online"),
        };
    }

    auto snapshot = NormalScenario(scenarioName);
    if (scenarioName == QStringLiteral("offline-cached")) {
        snapshot.connectionStatus = QStringLiteral("offline-cached");
    } else if (scenarioName == QStringLiteral("first-bootstrap")) {
        snapshot.connectionStatus = QStringLiteral("syncing");
    } else if (scenarioName == QStringLiteral("auth-expired")) {
        snapshot.connectionStatus = QStringLiteral("auth-expired");
    } else if (scenarioName == QStringLiteral("sync-gap")) {
        snapshot.connectionStatus = QStringLiteral("recovering-gap");
    }

    if (scenarioName == QStringLiteral("dense-chat-list")) {
        for (int index = 0; index < 28; ++index) {
            snapshot.conversations.push_back(ConversationRecord{
                .conversationId = QStringLiteral("dense-%1").arg(index),
                .title = QStringLiteral("设计讨论组 %1 · 一个较长的会话名称").arg(index + 1),
                .preview = QStringLiteral("这是用于验证大量会话、长文本和滚动稳定性的预览。"),
                .timestamp = QStringLiteral("周%1").arg(index % 7 + 1),
                .avatarColor = index % 2 == 0 ? QStringLiteral("#4361A8") : QStringLiteral("#6B4CA5"),
                .unreadCount = index % 5 == 0 ? 99 : index % 4,
                .pinned = index < 2,
                .muted = index % 6 == 0,
                .online = index % 3 == 0,
            });
        }
    }

    if (scenarioName == QStringLiteral("send-unknown")) {
        auto& messages = snapshot.messagesByConversation[QStringLiteral("alice")];
        messages.push_back(Message(-91, 0, 0, QStringLiteral("me"),
                                   QStringLiteral("连接中断后，这条消息的结果尚未确认。"), QStringLiteral("10:44"),
                                   true, MessageDeliveryState::Unknown));
    } else if (scenarioName == QStringLiteral("send-lifecycle")) {
        auto& messages = snapshot.messagesByConversation[QStringLiteral("alice")];
        messages.push_back(Message(-90, 0, 0, QStringLiteral("me"),
                                   QStringLiteral("这条消息可使用原 client_message_id 重试。"), QStringLiteral("10:43"),
                                   true, MessageDeliveryState::RetryableFailed));
    }

    if (scenarioName == QStringLiteral("friend-requests")) {
        snapshot.requests.push_back(RequestRecord{
            .requestId = QStringLiteral("request-4"),
            .displayName = QStringLiteral("沈予安"),
            .message = QStringLiteral("我们在 Qt 开发群见过。"),
            .avatarColor = QStringLiteral("#7A5AA6"),
            .kind = QStringLiteral("friend"),
            .status = QStringLiteral("pending"),
        });
    } else if (scenarioName == QStringLiteral("group-admin")) {
        snapshot.requests.push_back(RequestRecord{
            .requestId = QStringLiteral("group-request-1"),
            .displayName = QStringLiteral("顾远"),
            .message = QStringLiteral("申请加入 WIMI 设计组"),
            .avatarColor = QStringLiteral("#315FD6"),
            .kind = QStringLiteral("group"),
            .status = QStringLiteral("pending"),
        });
    }

    if (scenarioName == QStringLiteral("long-content")) {
        auto& messages = snapshot.messagesByConversation[QStringLiteral("alice")];
        messages.push_back(Message(-92, 3092, 44, QStringLiteral("alice"),
                                   QStringLiteral("长内容检查：中文、English、emoji 🚀🌌\n\n"
                                                  "消息气泡应当换行，并保持时间与状态区域稳定。"
                                                  "ThisIsAnIntentionallyLongUnbrokenTokenForLayoutTesting"),
                                   QStringLiteral("10:45"), false));
    }

    if (scenarioName == QStringLiteral("large-history")) {
        auto& messages = snapshot.messagesByConversation[QStringLiteral("alice")];
        constexpr int kAdditionalMessageCount = 2000;
        messages.reserve(messages.size() + kAdditionalMessageCount);
        for (int index = 0; index < kAdditionalMessageCount; ++index) {
            const bool outgoing = index % 3 == 0;
            messages.push_back(Message(
                -10000 - index, 100000 + index, 1000 + index, outgoing ? QStringLiteral("me") : QStringLiteral("alice"),
                QStringLiteral("历史消息 %1：用于验证长时间线的模型装载、虚拟化和滚动稳定性。").arg(index + 1),
                QStringLiteral("%1:%2")
                    .arg((index / 60) % 24, 2, 10, QLatin1Char('0'))
                    .arg(index % 60, 2, 10, QLatin1Char('0')),
                outgoing));
        }
    }

    return snapshot;
}

ClientSnapshot FakeScenarioRepository::NormalScenario(const QString& scenarioName)
{
    ClientSnapshot snapshot{
        .scenarioName = scenarioName,
        .connectionStatus = QStringLiteral("online"),
        .conversations =
            {
                {.conversationId = QStringLiteral("alice"),
                 .title = QStringLiteral("林晓"),
                 .preview = QStringLiteral("那我们下午把同步边界再过一遍"),
                 .timestamp = QStringLiteral("10:42"),
                 .avatarColor = QStringLiteral("#315FD6"),
                 .unreadCount = 2,
                 .pinned = true,
                 .muted = false,
                 .online = true},
                {.conversationId = QStringLiteral("group:4001"),
                 .title = QStringLiteral("WIMI 设计组"),
                 .preview = QStringLiteral("周宁：深色主题的对比度已调整"),
                 .timestamp = QStringLiteral("09:18"),
                 .avatarColor = QStringLiteral("#7656A8"),
                 .unreadCount = 8,
                 .pinned = true,
                 .muted = false,
                 .online = false},
                {.conversationId = QStringLiteral("chen"),
                 .title = QStringLiteral("陈屿"),
                 .preview = QStringLiteral("文档我看完了，方向没问题"),
                 .timestamp = QStringLiteral("昨天"),
                 .avatarColor = QStringLiteral("#247B6B"),
                 .unreadCount = 0,
                 .pinned = false,
                 .muted = false,
                 .online = false},
                {.conversationId = QStringLiteral("group:4002"),
                 .title = QStringLiteral("服务端路线"),
                 .preview = QStringLiteral("Outbox 阶段放到同步闭环之后"),
                 .timestamp = QStringLiteral("周二"),
                 .avatarColor = QStringLiteral("#A05A4E"),
                 .unreadCount = 0,
                 .pinned = false,
                 .muted = true,
                 .online = false},
            },
    };

    snapshot.contacts = {
        {.userId = QStringLiteral("alice"),
         .displayName = QStringLiteral("林晓"),
         .statusText = QStringLiteral("在线"),
         .avatarColor = QStringLiteral("#315FD6"),
         .online = true,
         .favorite = true},
        {.userId = QStringLiteral("zhou"),
         .displayName = QStringLiteral("周宁"),
         .statusText = QStringLiteral("刚刚在线"),
         .avatarColor = QStringLiteral("#7656A8"),
         .online = false,
         .favorite = true},
        {.userId = QStringLiteral("chen"),
         .displayName = QStringLiteral("陈屿"),
         .statusText = QStringLiteral("昨天在线"),
         .avatarColor = QStringLiteral("#247B6B"),
         .online = false,
         .favorite = false},
        {.userId = QStringLiteral("gu"),
         .displayName = QStringLiteral("顾远"),
         .statusText = QStringLiteral("在线"),
         .avatarColor = QStringLiteral("#A05A4E"),
         .online = true,
         .favorite = false},
        {.userId = QStringLiteral("shen"),
         .displayName = QStringLiteral("沈予安"),
         .statusText = QStringLiteral("周一在线"),
         .avatarColor = QStringLiteral("#7A5AA6"),
         .online = false,
         .favorite = false},
    };

    snapshot.requests = {
        {.requestId = QStringLiteral("request-1"),
         .displayName = QStringLiteral("陆清"),
         .message = QStringLiteral("你好，我在开源社区看到了 WIMI。"),
         .avatarColor = QStringLiteral("#367A91"),
         .kind = QStringLiteral("friend"),
         .status = QStringLiteral("pending")},
        {.requestId = QStringLiteral("request-2"),
         .displayName = QStringLiteral("许舟"),
         .message = QStringLiteral("我是后端讨论组的许舟。"),
         .avatarColor = QStringLiteral("#8A653A"),
         .kind = QStringLiteral("friend"),
         .status = QStringLiteral("accepted")},
        {.requestId = QStringLiteral("request-3"),
         .displayName = QStringLiteral("唐可"),
         .message = QStringLiteral("申请加入 WIMI 设计组"),
         .avatarColor = QStringLiteral("#B15474"),
         .kind = QStringLiteral("group"),
         .status = QStringLiteral("declined")},
    };

    snapshot.messagesByConversation.insert(
        QStringLiteral("alice"),
        {
            Message(-1, 3001, 38, QStringLiteral("alice"), QStringLiteral("Qt 6.10 的开发环境已经确认了吗？"),
                    QStringLiteral("10:31"), false),
            Message(-2, 3002, 39, QStringLiteral("me"), QStringLiteral("确认了，先把客户端骨架和响应式主界面落下来。"),
                    QStringLiteral("10:33"), true),
            Message(-3, 3003, 40, QStringLiteral("alice"),
                    QStringLiteral("颜色就用星空蓝，但信息密度可以参考 Telegram。"), QStringLiteral("10:36"), false),
            Message(-4, 3004, 41, QStringLiteral("me"),
                    QStringLiteral("好。界面只表达领域状态，不绑定当前服务端的临时 seq 语义。"),
                    QStringLiteral("10:39"), true, MessageDeliveryState::Delivered),
            Message(-5, 3005, 42, QStringLiteral("alice"), QStringLiteral("那我们下午把同步边界再过一遍。"),
                    QStringLiteral("10:42"), false),
        });

    snapshot.messagesByConversation.insert(
        QStringLiteral("group:4001"),
        {
            Message(-10, 4001, 12, QStringLiteral("zhou"),
                    QStringLiteral("浅色和深色主题都改成语义色，不在页面里直接写颜色。"), QStringLiteral("09:11"),
                    false),
            Message(-11, 4002, 13, QStringLiteral("me"), QStringLiteral("收到，组件只依赖 Theme 和 Tokens。"),
                    QStringLiteral("09:14"), true),
            Message(-12, 4003, 14, QStringLiteral("zhou"), QStringLiteral("深色主题的对比度已调整。"),
                    QStringLiteral("09:18"), false),
        });

    snapshot.messagesByConversation.insert(
        QStringLiteral("chen"), {Message(-20, 5001, 7, QStringLiteral("chen"),
                                         QStringLiteral("文档我看完了，方向没问题。"), QStringLiteral("昨天"), false)});

    snapshot.messagesByConversation.insert(
        QStringLiteral("group:4002"), {Message(-30, 6001, 21, QStringLiteral("backend"),
                                               QStringLiteral("先完成持久幂等接受与断线同步，再扩展 Kafka 和路由层。"),
                                               QStringLiteral("周二"), false)});

    return snapshot;
}

} // namespace wimi::client
