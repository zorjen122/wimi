#pragma once

#include "Const.h"
#include <boost/asio.hpp>
#include <boost/beast.hpp>

namespace wimi
{

using tcp = boost::asio::ip::tcp;
namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
class HttpSession : public std::enable_shared_from_this<HttpSession>
{
    friend class Service;

  public:
    using ResponsePtr = std::shared_ptr<http::response<http::dynamic_body>>;
    using RequestPtr = std::shared_ptr<http::request<http::dynamic_body>>;

    HttpSession(boost::asio::io_context& ioc);
    void Start();
    void PreParseGetParam();
    tcp::socket& GetSocket() { return socket; }
    ResponsePtr GetResponse() { return response; }
    RequestPtr GetRequest() { return request; }

  private:
    void CheckDeadline();
    void WriteResponse();
    void HandleRequest();
    void ClearStatusMessage();
    tcp::socket socket;
    // The buffer for performing reads.
    beast::flat_buffer _buffer{8192};

    // The request message.
    RequestPtr request;

    // The response message.
    ResponsePtr response;

    // The timer for putting a deadline on connection processing.
    net::steady_timer deadline{socket.get_executor(), std::chrono::seconds(60)};

    std::string url;
    std::unordered_map<std::string, std::string> paramMap;
};
}; // namespace wimi