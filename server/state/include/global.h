#pragma once
#include <memory>
#include <string>
#include <vector>

namespace wimi
{

struct ServiceNodeInfo {
    std::string id;
    std::string host;
    unsigned short port{0};
    std::string status{"active"};
    unsigned int weight{1};

    bool active() const { return status == "active" && !host.empty() && port != 0; }
};

class ImNode
{
  public:
    using ptr = std::shared_ptr<ImNode>;
    ImNode(const std::string& ip, const std::string& port, const std::string& rpcPort, const std::string& status)
        : ip(ip), port(port), rpcPort(rpcPort), status(status)
    {
    }
    bool empty() const { return ip.empty() || port.empty(); }
    std::string getIp() const { return ip; }
    std::string getRpcPort() const { return rpcPort; }
    std::string getPort() const { return port; }
    std::string getStatus() const { return status; }
    void setStatus(const std::string& status) { this->status = status; }
    void appendConnection(long connection) { connections.push_back(connection); }

  private:
    std::string ip;
    std::string port;
    std::string rpcPort;
    std::string status;
    std::vector<long> connections;
};
}; // namespace wimi
