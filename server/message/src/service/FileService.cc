#include "FileService.h"

#include "Const.h"
#include "FileRpc.h"
#include "Logger.h"
#include "Redis.h"

#include <exception>
#include <grpcpp/support/status.h>
#include <string>

namespace wimi
{

TcpPacket FileService::Upload(uint32_t msgID, TcpPacket& request)
{
    TcpPacket rsp;
    int64_t clientSeq = request.seq();
    int64_t uid = request.uid();
    std::string data = request.data();
    std::string fileName = request.file_name();
    rpc::FileType type;

    rsp.set_seq(clientSeq);

    bool hasUserMsgId = db::RedisDao::GetInstance()->getUserMsgId(uid, clientSeq);
    static short __expireUserMsgId = 10;
    if (hasUserMsgId) {
        LOG_INFO(businessLogger, "重复消息, 客户端消息序列号为: {}", clientSeq);
        db::RedisDao::GetInstance()->expireUserMsgId(uid, clientSeq, __expireUserMsgId);
        rsp.set_error(ErrorCodes::RepeatMessage);
        rsp.set_message("重复消息");
        return rsp;
    }

    std::string tmpType = request.file_type();
    if (tmpType == "TEXT") {
        type = rpc::FileType::TEXT;
    } else if (tmpType == "IMAGE") {
        type = rpc::FileType::IMAGE;
    } else {
        LOG_ERROR(businessLogger, "文件传输错误");
        rsp.set_error(ErrorCodes::FileTypeError);
        rsp.set_message("文件类型错误");
        return rsp;
    }

    rpc::UploadRequest rpcRequest;
    rpc::UploadResponse rpcResponse;

    // 当set_allocated_chunk时，protobuf会自动管理 chunk 内存
    rpc::FileChunk* fileChunk = new rpc::FileChunk();
    fileChunk->set_seq(clientSeq);
    fileChunk->set_filename(fileName);
    fileChunk->set_type(type);
    fileChunk->set_data(data);

    rpcRequest.set_user_id(uid);
    rpcRequest.set_allocated_chunk(fileChunk);

    try {
        grpc::Status status = rpc::FileRpc::GetInstance()->forwardUpload(rpcRequest, rpcResponse);

        if (status.ok()) {
            rsp.set_error(ErrorCodes::Success);
        } else {
            rsp.set_error(ErrorCodes::RPCFailed);
            rsp.set_message("RPC 失败: " + status.error_message());
        }
    } catch (const std::exception& e) {
        rsp.set_error(ErrorCodes::InternalError);
        rsp.set_message("系统异常: " + std::string(e.what()));
    }

    return rsp;
}

} // namespace wimi
