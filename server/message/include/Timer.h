#pragma once
#include <boost/asio.hpp>
#include <boost/system/detail/error_code.hpp>
#include <functional>
#include <spdlog/spdlog.h>

using boost::asio::io_context;
using boost::asio::steady_timer;
using boost::system::error_code;

class Timer
{
  public:
    Timer(io_context& ioc, size_t seconds);
    ~Timer();
    void startTimer(std::function<void(error_code)> resetFunc, std::function<void(error_code)> tickleFunc,
                    bool round = false);
    Timer& getTimer();

  private:
    io_context& ioc;
    steady_timer timer;
    size_t seconds;
};