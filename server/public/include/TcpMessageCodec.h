#pragma once

#include <string>

#include "Const.h"
#include "tcp_message.pb.h"

namespace wimi
{

using TcpPacket = protocol::Packet;

inline bool ParseTcpPacket(const std::string& data, TcpPacket& packet) { return packet.ParseFromString(data); }

inline std::string SerializeTcpPacket(const TcpPacket& packet)
{
    std::string data;
    packet.SerializeToString(&data);
    return data;
}

inline TcpPacket MakeErrorPacket(int error, const std::string& message = {})
{
    TcpPacket packet;
    packet.set_error(error);
    // 所有通用错误响应都由中央错误分类生成 retryable，避免调用点遗漏。
    packet.set_retryable(isRetryableError(error));
    if (!message.empty()) {
        packet.set_message(message);
    }
    return packet;
}

inline int TcpPacketError(const TcpPacket& packet) { return packet.has_error() ? packet.error() : ErrorCodes::Success; }

inline std::string TcpPacketDebugString(const TcpPacket& packet) { return packet.ShortDebugString(); }

} // namespace wimi
