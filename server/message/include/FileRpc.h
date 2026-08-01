#pragma once
#include "Const.h"
#include "RpcPool.h"
#include "file.grpc.pb.h"
#include "file.pb.h"
#include <grpcpp/support/status.h>
#include <memory>

namespace wimi::rpc
{

using file::FileChunk;
using file::FileService;
using file::FileType;
using file::SendRequest;
using file::SendResponse;
using file::TransferStatus;
using file::UploadRequest;
using file::UploadResponse;
class FileRpc : public Singleton<FileRpc>
{
  public:
    using Ptr = std::shared_ptr<FileRpc>;
    FileRpc();
    ~FileRpc();
    grpc::Status forwardUpload(const UploadRequest& req, UploadResponse& resp);
    auto getPoolSize() const;

  private:
    std::unique_ptr<RpcPool<FileService>> pool = nullptr;
};

}; // namespace wimi::rpc