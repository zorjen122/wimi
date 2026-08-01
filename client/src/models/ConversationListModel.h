#pragma once

#include "domain/ClientSnapshot.h"

#include <QAbstractListModel>

namespace wimi::client
{

class ConversationListModel final : public QAbstractListModel
{
    Q_OBJECT

  public:
    enum Role {
        ConversationIdRole = Qt::UserRole + 1,
        SourceIndexRole,
        TitleRole,
        PreviewRole,
        TimestampRole,
        AvatarColorRole,
        RemoteConversationIdRole,
        UnreadCountRole,
        PinnedRole,
        MutedRole,
        OnlineRole,
    };
    Q_ENUM(Role)

    explicit ConversationListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void SetRecords(QVector<ConversationRecord> records);
    void Prepend(ConversationRecord record);
    const ConversationRecord* RecordAt(int index) const;
    int IndexOf(const QString& conversationId) const;
    bool SetRemoteConversationId(const QString& conversationId, std::int64_t remoteConversationId);
    void UpdatePreview(const QString& conversationId, const QString& preview, const QString& timestamp);
    bool SetUnreadCount(const QString& conversationId, int unreadCount);
    bool TogglePinned(int index);
    bool ToggleMuted(int index);
    bool MarkRead(int index);

  private:
    QVector<ConversationRecord> records_;
};

} // namespace wimi::client
