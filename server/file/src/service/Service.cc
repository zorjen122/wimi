#include "Service.h"
#include "Configer.h"
#include "Logger.h"
#include "file.pb.h"
#include <filesystem>
#include <fstream>
#include <grpcpp/support/status.h>
#include <string>
namespace fs = std::filesystem;

namespace wimi::rpc
{

FileWorker::FileWorker() : stopEnable(false)
{
    workThread = std::thread([this]() {
        while (!stopEnable) {
            std::unique_lock<std::mutex> lock(executeMutex);
            cond.wait(lock, [this]() {
                if (stopEnable) {
                    return true;
                }
                if (tasks.empty()) {
                    return false;
                }
                return true;
            });
            if (stopEnable) {
                break;
            }
            auto task = tasks.front();
            task->Process();
            tasks.pop();
        }
    });
}

FileWorker::~FileWorker()
{
    stopEnable = true;
    cond.notify_one();

    if (workThread.joinable())
        workThread.join();
}

void FileWorker::PushTask(Task task)
{
    {
        std::lock_guard<std::mutex> lock(executeMutex);
        tasks.push(task);
    }
    cond.notify_one();
}
FileServiceImpl::FileServiceImpl(int workerSize)
{
    for (int i = 0; i < workerSize; i++) {
        workers.emplace_back(std::make_unique<FileWorker>());
    }
}
FileServiceImpl::~FileServiceImpl()
{
    for (auto& worker : workers)
        worker.reset();
}

std::string FileServiceImpl::getFileType(const std::string& filename)
{
    // get .xxx
    std::string suffix = filename.substr(filename.rfind(".") + 1);
    return suffix;
}

void FileServiceImpl::PushTask(FileWorker::Task task)
{
    static int routeCount = 0;
    workers[routeCount]->PushTask(task);
    routeCount++;
    if (routeCount >= workers.size())
        routeCount = 0;
}

grpc::Status FileServiceImpl::Upload(grpc::ServerContext* context, const file::UploadRequest* request,
                                     file::UploadResponse* response)
{
    std::shared_ptr<file::UploadRequest> requestTemp(new file::UploadRequest(*request));
    auto handle = [requestTemp] {
        LOG_INFO(businessLogger, "Upload file request received");
        if (!requestTemp->has_chunk()) {
            LOG_ERROR(netLogger, "Missing file chunk, request size: {}, data: {}", requestTemp->ByteSizeLong(),
                      requestTemp->DebugString());
            return;
        }

        long uid = requestTemp->user_id();
        auto fileChunk = requestTemp->chunk();
        if (fileChunk.filename().empty()) {
            LOG_ERROR(netLogger, "Invalid filename");
            return;
        }

        std::string filename = fileChunk.filename();

        // 构建完整保存路径
        fs::path saveDir = fs::path(Configer::getSaveFilePath()) / std::to_string(uid);
        fs::path saveFilePath = saveDir / filename;

        try {
            if (!fs::exists(saveDir)) {
                fs::create_directories(saveDir); // 递归创建所有不存在的目录
            }

            std::ofstream ofs(saveFilePath, std::ios::binary | std::ios::app);
            if (!ofs.is_open()) {
                throw std::runtime_error("Failed to open file: " + saveFilePath.string());
            }
            ofs.write(fileChunk.data().c_str(), fileChunk.data().size());
        } catch (const fs::filesystem_error& e) {
            std::cerr << "Filesystem error: " << e.what() << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << std::endl;
        }
    };

    auto task = std::make_shared<UnifiedCallData<file::UploadRequest, file::UploadResponse>>(context, request, response,
                                                                                             handle);

    PushTask(task);

    return grpc::Status::OK;
}

FileServer::FileServer(size_t workerSize) : stopEnable(true), workerSize(workerSize)
{
    if (workerSize > std::thread::hardware_concurrency()) {
        LOG_ERROR(netLogger, "workerSize is too large, it will be set to hardware_concurrency");
        workerSize = std::thread::hardware_concurrency();
    }
}

FileServer::~FileServer() { Stop(); }

void FileServer::Run(unsigned short port)
{
    std::string serverAddress = "0.0.0.0:" + std::to_string(port);
    ServerBuilder builder;
    builder.AddListeningPort("0.0.0.0:" + std::to_string(port), grpc::InsecureServerCredentials());

    service.reset(new FileServiceImpl(workerSize));
    builder.RegisterService(service.get());

    // 10MB MSS客户端发送接口限制
    builder.SetMaxReceiveMessageSize(10 * 1024 * 1024);
    server = builder.BuildAndStart();

    LOG_INFO(netLogger, "Server listening on " + serverAddress);

    stopEnable = false;
    server->Wait();
}

void FileServer::Stop()
{
    if (server) {
        server->Shutdown();
    }
    if (service) {
        service.reset();
    }
}
bool FileServer::stopped() const { return stopEnable.load(); }

}; // namespace wimi::rpc
