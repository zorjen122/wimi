#include "gate.h"
#include "Const.h"

namespace wimi
{

Gate::Gate(net::io_context& iocontext, const std::string& host, const std::string& port)
    : context(iocontext), stream(iocontext)
{
    if (host.empty() || port.empty())
        throw std::invalid_argument("host or port is empty");

    Defer _([this]() { __clearStatusMessage(); });

    tcp::resolver resolver(context);

    endpoint = resolver.resolve(host, port);

    boost::system::error_code ec;
    stream.connect(endpoint, ec);
    if (ec.failed())
        throw std::runtime_error("connect failed: " + ec.message());

    request.method(http::verb::get);
    request.target(__GateTestPath__);
    request.version(11);
    request.set(http::field::host, host);
    request.set(http::field::content_type, "application/json");

    http::write(stream, request, ec);
    if (ec.failed())
        throw std::runtime_error("write failed: " + ec.message());

    http::read(stream, buffer, response, ec);
    if (ec.failed())
        throw std::runtime_error("read failed: " + ec.message());

    auto bodyBuffer = response.body().data();
    auto stringBody = beast::buffers_to_string(bodyBuffer);

    LOG_INFO(wimi::businessLogger, "response message: {}", stringBody);
}

std::pair<Endpoint, int> Gate::signIn(const std::string& username, const std::string& password)
{
    LOG_INFO(wimi::businessLogger, "sign in as {}", username);

    Defer _([this]() { __clearStatusMessage(); });

    boost::system::error_code ec;
    stream.connect(endpoint, ec);
    if (ec.failed())
        throw std::runtime_error("connect failed: " + ec.message());

    request.method(http::verb::post);
    request.target(__GateSigninPath__);

    Json::Value requestData;
    requestData["username"] = username;
    requestData["password"] = password;

    request.body() = requestData.toStyledString();
    request.prepare_payload();
    http::write(stream, request, ec);
    if (ec.failed())
        throw std::runtime_error("write failed: " + ec.message());
    http::read(stream, buffer, response, ec);
    if (ec.failed())
        throw std::runtime_error("read failed: " + ec.message());

    auto stringBody = __parseResponse();
    LOG_INFO(wimi::businessLogger, "gate sign-in response received");

    Json::Reader reader;
    Json::Value responseData;
    reader.parse(stringBody, responseData);

    int init = responseData["init"].asInt();

    Endpoint chatEndpoint(responseData["ip"].asString(), responseData["port"].asString());

    auto uid = responseData["uid"].asInt64();
    users[username] = std::make_shared<db::User>(0, uid, username, password, "null");

    return {chatEndpoint, init};
}

bool Gate::signUp(const std::string& username, const std::string& password, const std::string& email)
{
    LOG_INFO(wimi::businessLogger, "sign up as {}", username);

    Defer _([this]() { __clearStatusMessage(); });

    boost::system::error_code ec;
    stream.connect(endpoint, ec);
    if (ec.failed())
        throw std::runtime_error("connect failed: " + ec.message());

    request.method(http::verb::post);
    request.target(__GateSignupPath__);

    Json::Value requestData;
    requestData["username"] = username;
    requestData["password"] = password;
    requestData["email"] = email;

    request.body() = requestData.toStyledString();
    request.prepare_payload();

    LOG_INFO(wimi::businessLogger, "http-write({})", request.target());
    http::write(stream, request, ec);
    if (ec.failed())
        throw std::runtime_error("write failed: " + ec.message());
    http::read(stream, buffer, response, ec);
    if (ec.failed())
        throw std::runtime_error("read failed: " + ec.message());

    auto stringBody = __parseResponse();
    LOG_INFO(wimi::businessLogger, "response message: {} | status:{}", stringBody, response.result_int());

    Json::Value responseData = __parseJson(stringBody);
    auto uid = responseData["uid"].asInt64();
    users[username] = std::make_shared<db::User>(0, uid, username, password, email);

    return true;
}
bool Gate::signOut() { return true; }
bool Gate::fogetPassword(const std::string& username) { return true; }

std::string Gate::__parseResponse()
{
    auto bodyBuffer = response.body().data();
    return beast::buffers_to_string(bodyBuffer);
}
Json::Value __parseJson(const std::string& source)
{
    Json::Reader reader;
    Json::Value responseData;
    bool parseSuccess = reader.parse(source, responseData);
    if (parseSuccess == false)
        throw std::runtime_error("parse json failed");
    return responseData;
}
void Gate::__clearStatusMessage()
{
    request.body().clear();
    response.body().clear();
    buffer.clear();
}

} // namespace wimi
