#pragma once
#include <string>

#include "Const.h"
#include <boost/asio.hpp>

using tcp = boost::asio::ip::tcp;
namespace net = boost::asio;

namespace wimi
{
class GateServer : public std::enable_shared_from_this<GateServer>
{
  public:
    GateServer(net::io_context& ioc, unsigned short& port);
    void Start();

  private:
    net::io_context& gateContext;
    tcp::acceptor acceptor;
};

} // namespace wimi