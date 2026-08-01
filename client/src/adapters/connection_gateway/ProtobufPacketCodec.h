#pragma once

#include "tcp_message.qpb.h"

#include <QByteArray>
#include <QString>

namespace wimi::client
{

bool SerializeProtobufPacket(const wimi::protocol::Packet& packet, QByteArray* payload);
bool ParseProtobufPacket(const QByteArray& payload, wimi::protocol::Packet* packet);
QString PacketSendDateTimeOrEmpty(const wimi::protocol::Packet& packet);

} // namespace wimi::client
