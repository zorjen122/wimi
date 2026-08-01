#include "Timer.h"
#include <boost/asio/detail/chrono.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/system/detail/error_code.hpp>

Timer::Timer(boost::asio::io_context& ioContext, size_t second)
    : ioc(ioContext), timer(ioContext, boost::asio::chrono::seconds(second))
{
}

Timer::~Timer() {}
void Timer::startTimer(std::function<void(error_code)> resetHandle, std::function<void(error_code)> tickleHandle,
                       bool round)
{
    timer.async_wait([resetHandle, tickleHandle, round](const boost::system::error_code& ec) {
        if (!ec) {
            spdlog::info("[Timer normal!]");
        } else if (ec == boost::asio::error::timed_out) {
            spdlog::info("[Timer tickle!]");
            tickleHandle(ec);
            if (round) {
                resetHandle(ec);
            }
        }
    });
}

Timer& Timer::getTimer() { return *this; }