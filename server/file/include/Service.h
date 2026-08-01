#pragma once

#include "file.grpc.pb.h"
#include "file.pb.h"
#include <condition_variable>
#include <grpcpp/completion_queue.h>
#include <grpcpp/server_builder.h>
#include <grpcpp/server_context.h>
#include <queue>
#include <thread>
#include <vector>

namespace wimi::rpc
{

using file::FileChunk;
using file::FileService;
using file::FileType;
using file::TransferStatus;
using file::UploadRequest;
using file::UploadResponse;

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;

class FileTask
{
  public:
    virtual ~FileTask() = default;
    virtual void Process() = 0;
};

template <typename RequestType, typename ResponseType>
class UnifiedCallData : public FileTask,
                        // 此用于对FileTask多态调用约束类型
                        public std::shared_ptr<UnifiedCallData<RequestType, ResponseType>>
{
  public:
    using Ptr = std::shared_ptr<UnifiedCallData<RequestType, ResponseType>>;
    using ProcessFunc = std::function<void()>;

    UnifiedCallData(grpc::ServerContext* context, const RequestType* request, ResponseType* response,
                    ProcessFunc processor)
        : context_(context), request_(request), response_(response), processor_(processor)
    {
    }

    void Process() override { processor_(); }

  private:
    grpc::ServerContext* context_;
    const RequestType* request_;
    ResponseType* response_;
    ProcessFunc processor_;
};

class FileWorker
{
  public:
    using Task = std::shared_ptr<FileTask>;
    FileWorker();
    ~FileWorker();
    void PushTask(Task task);

  private:
    std::thread workThread;
    std::queue<Task> tasks;
    std::atomic<bool> stopEnable;
    std::mutex executeMutex;
    std::condition_variable cond;
};

class FileServiceImpl final : public FileService::Service
{
  public:
    FileServiceImpl(int workerSize);
    ~FileServiceImpl();
    grpc::Status Upload(grpc::ServerContext* context, const file::UploadRequest* request,
                        file::UploadResponse* response) override;

  private:
    std::string getFileType(const std::string& filename);

    void PushTask(FileWorker::Task task);

  private:
    std::vector<std::unique_ptr<FileWorker>> workers;
};

class FileServer final
{
  public:
    FileServer(size_t workerSize);
    ~FileServer();

    void Run(unsigned short port);
    void Stop();
    bool stopped() const;

    std::atomic<bool> stopEnable;
    std::unique_ptr<grpc::Server> server;
    std::unique_ptr<FileServiceImpl> service;
    size_t workerSize;
};

}; // namespace wimi::rpc
