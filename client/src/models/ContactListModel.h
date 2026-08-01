#pragma once

#include "domain/ClientSnapshot.h"

#include <QAbstractListModel>

namespace wimi::client
{

class ContactListModel final : public QAbstractListModel
{
    Q_OBJECT

  public:
    enum Role {
        UserIdRole = Qt::UserRole + 1,
        SourceIndexRole,
        DisplayNameRole,
        StatusTextRole,
        AvatarColorRole,
        OnlineRole,
        FavoriteRole,
    };
    Q_ENUM(Role)

    explicit ContactListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void SetRecords(QVector<ContactRecord> records);
    const ContactRecord* RecordAt(int index) const;
    bool ToggleFavorite(int index);

  private:
    QVector<ContactRecord> records_;
};

} // namespace wimi::client
