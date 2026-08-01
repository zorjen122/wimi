#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

namespace wimi::client
{

struct TcpFrame {
    quint32 serviceId{};
    QByteArray payload;
};

class TcpFrameCodec final
{
  public:
    static constexpr quint32 kHeaderSize = 8;
    static constexpr quint32 kMaximumPayloadSize = 10 * 1024 * 1024;

    static QByteArray Encode(quint32 serviceId, const QByteArray& payload);

    QVector<TcpFrame> Feed(const QByteArray& bytes);
    bool HasError() const;
    QString ErrorString() const;
    void Reset();

  private:
    QByteArray buffer_;
    QString error_;
};

} // namespace wimi::client
