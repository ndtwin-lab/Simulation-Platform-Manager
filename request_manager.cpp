/*
 * Copyright (c) 2025-present
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * NDTwin core contributors (as of January 15, 2026):
 *     Prof. Shie-Yuan Wang <National Yang Ming Chiao Tung University; CITI, Academia Sinica>
 *     Ms. Xiang-Ling Lin <CITI, Academia Sinica>
 *     Mr. Po-Yu Juan <CITI, Academia Sinica>
 *     Mr. Tsu-Li Mou <CITI, Academia Sinica> 
 *     Mr. Zhen-Rong Wu <National Taiwan Normal University>
 *     Mr. Ting-En Chang <University of Wisconsin, Milwaukee>
 *     Mr. Yu-Cheng Chen <National Yang Ming Chiao Tung University>
 */
#include <iostream>
#include <fstream>
#include <random>
#include <string>
#include <unordered_map>
#include <thread>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/stacktrace.hpp>
#include <boost/asio.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/config.hpp>
#include <nlohmann/json.hpp>

#include "settings/request_manager.hpp"
#include "utils/Logger.hpp"
#include "types/app.hpp"

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;
using json = nlohmann::json;

static std::unordered_map<std::string, std::string> app_id2ip{{"power", "127.0.0.1"}};
static std::unordered_map<std::string, std::string> app_id2port{{"power", "8000"}};

class HttpSession : public std::enable_shared_from_this<HttpSession>
{
public:
    explicit HttpSession(tcp::socket socket, net::io_context& ioc) : _in_stream(std::move(socket)), _resolver(ioc), _out_stream(ioc), _strand(net::make_strand(ioc))
    {
        auto remote_endpoint = _in_stream.socket().remote_endpoint();
        std::string remote_ip = remote_endpoint.address().to_string();
        unsigned short remote_port = remote_endpoint.port();
        SPDLOG_LOGGER_INFO(Logger::instance(), "Get Connection: IP: {}, port: {}", remote_ip, remote_port);
    }

    ~HttpSession()
    {
        _in_stream.close();
        _out_stream.close();
    }

    void run()
    {
        do_read();
    }

private:
    beast::tcp_stream _in_stream;
    beast::flat_buffer _buffer;
    http::request<http::string_body> _req;

    tcp::resolver _resolver;
    beast::tcp_stream _out_stream;
    net::strand<net::io_context::executor_type> _strand;
    bool has_connect = false;

    void do_read()
    {
        _req = {}; // reset
        _buffer.consume(_buffer.size());

        auto self = shared_from_this();
        http::async_read(_in_stream, _buffer, _req,
        [self](beast::error_code ec, std::size_t)
        {
            if (ec == beast::http::error::end_of_stream || ec == net::error::eof)
            {
                auto remote_endpoint = self->_in_stream.socket().remote_endpoint();
                std::string remote_ip = remote_endpoint.address().to_string();
                unsigned short remote_port = remote_endpoint.port();
                SPDLOG_LOGGER_WARN(Logger::instance(), "Client closed connection {}:{}", remote_ip, remote_port);
                beast::error_code ec;
                self->_in_stream.socket().shutdown(tcp::socket::shutdown_both, ec);
                return;
            }

            if (ec)
            {
                SPDLOG_LOGGER_ERROR(Logger::instance(), "async_read failed: {}", ec.message());
                return;
            }

            self->handle_request();
        });
    }

    void handle_request()
    {
        SPDLOG_LOGGER_INFO(Logger::instance(), "Got request: {} {}", std::string(_req.method_string()), std::string(_req.target()));
        SPDLOG_LOGGER_INFO(Logger::instance(), "body: {}", _req.body());
        SPDLOG_LOGGER_INFO(Logger::instance(), "keep alive: {}", _req.keep_alive());

        if (_req.method() == http::verb::post && _req.target() == request_manager_target_for_app)
        {
            try
            {
                auto res = std::make_shared<http::response<http::string_body>>(http::status::ok, _req.version());
                res->set(http::field::content_type, "text/plain");
                res->keep_alive(_req.keep_alive());
                res->body() = "已收到 Request\n";
                res->prepare_payload();

                auto self = shared_from_this();
                http::async_write(_in_stream, *res, [self, res](beast::error_code ec, std::size_t)
                {
                    if (ec)
                    {
                        SPDLOG_LOGGER_ERROR(Logger::instance(), "async_write failed: {}", ec.message());
                        return;
                    }

                    SPDLOG_LOGGER_INFO(Logger::instance(), "Wait for next request...");
                    self->do_read(); // Go back to reading the next stroke
                });

                if (!has_connect)
                {
                    boost::system::error_code ec;
                    const auto results = _resolver.resolve(sim_server_ip, sim_server_port);
                    _out_stream.connect(results, ec);
                    if (ec)
                    {
                        SPDLOG_LOGGER_ERROR(Logger::instance(), "Failed to connect to {}:{}: {}", sim_server_ip, sim_server_port, ec.message());
                        return;
                    }
                    has_connect = true;
                }

                forwarding(sim_server_ip, sim_server_target, _req.body());
            }
            catch (std::exception &e)
            {
                SPDLOG_LOGGER_ERROR(Logger::instance(), "handle request failed: {}", e.what());
                do_read(); // Go back to reading the next stroke
            }
        }
        else if (_req.method() == http::verb::post && _req.target() == request_manager_target_for_sim_server)
        {
            try
            {
                auto res = std::make_shared<http::response<http::string_body>>(http::status::ok, _req.version());
                res->set(http::field::content_type, "text/plain");
                res->keep_alive(_req.keep_alive());
                res->body() = "Received Result\n";
                res->prepare_payload();

                auto self = shared_from_this();
                http::async_write(_in_stream, *res, [self, res](beast::error_code ec, std::size_t)
                {
                    if (ec)
                    {
                        SPDLOG_LOGGER_ERROR(Logger::instance(), "async_write failed: {}", ec.message());
                        return;
                    }

                    SPDLOG_LOGGER_INFO(Logger::instance(), "Wait for next request...");
                    self->do_read(); // Go back to reading the next stroke
                });

                json j = json::parse(_req.body());
                SimulationResult sim_res = j.get<SimulationResult>();

                if (!has_connect)
                {
                    boost::system::error_code ec;
                    const auto results = _resolver.resolve(app_id2ip[sim_res.app_id], app_id2port[sim_res.app_id]);
                    _out_stream.connect(results, ec);
                    if (ec)
                    {
                        SPDLOG_LOGGER_ERROR(Logger::instance(), "Failed to connect to {}:{}: {}", app_id2ip[sim_res.app_id], app_id2port[sim_res.app_id], ec.message());
                        return;
                    }
                    has_connect = true;
                }

                forwarding(app_id2ip[sim_res.app_id], app_target, _req.body());
            }
            catch (std::exception &e)
            {
                SPDLOG_LOGGER_ERROR(Logger::instance(), "handle request failed: {}", e.what());
                do_read(); // Go back to reading the next stroke
            }
        }
        else
        {
            SPDLOG_LOGGER_ERROR(Logger::instance(),
                                "Unsupported method or path: {}, {}",
                                std::string(_req.method_string()), std::string(_req.target()));
        }
    }

    // TODO: Instead, use a thread pool (1 or 2 threads are enough), and use blocking read/write operations within each thread.
    void forwarding(const std::string &ip, const std::string &target, std::string &body)
    {
        auto req = std::make_shared<http::request<http::string_body>>(http::verb::post, target, _req.version());
        req->set(http::field::host, ip);
        req->keep_alive(_req.keep_alive());
        req->set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
        req->set(http::field::content_type, "application/json");
        req->body() = body;
        req->prepare_payload();

        auto target_ptr = std::make_shared<std::string>(target);
        auto buffer = std::make_shared<beast::flat_buffer>();
        auto res = std::make_shared<http::response<http::string_body>>();
        auto self = shared_from_this();

        // Post/dispatch to strand ensures no duplicate async operations.
        net::post(_strand,
        [self, req, target_ptr, buffer, res]()
        {
            SPDLOG_LOGGER_INFO(Logger::instance(), "start forwarding {} to {}", std::string(req->method_string()), *target_ptr);

            beast::error_code ec;

            http::write(self->_out_stream, *req, ec);
            if (ec)
            {
                SPDLOG_LOGGER_ERROR(Logger::instance(), "forwarding write failed: {}", ec.message());
                return;
            }

            SPDLOG_LOGGER_INFO(Logger::instance(), "forwarding {} to {}", std::string(req->method_string()), *target_ptr);
            SPDLOG_LOGGER_INFO(Logger::instance(), "forwarding body = {}", req->body());

            // Receive response
            beast::flat_buffer buffer;
            http::response<http::string_body> res;
            http::read(self->_out_stream, buffer, res, ec);

            if (ec)
            {
                SPDLOG_LOGGER_ERROR(Logger::instance(), "forwarding read failed: {}", ec.message());
                return;
            }

            SPDLOG_LOGGER_INFO(Logger::instance(), "forwarding Response: code = {}", res.result_int());
            SPDLOG_LOGGER_INFO(Logger::instance(), "forwarding body = {}", res.body());
        });
    }
};

int main(int argc, char *argv[])
{
    auto cfg = Logger::parse_cli_args(argc, argv);
    Logger::init(cfg);
    SPDLOG_LOGGER_INFO(Logger::instance(), "Logger Loads Successfully!");

    net::io_context ioc;
    auto work = net::make_work_guard(ioc); // Prevent io_context from exiting prematurely
    tcp::acceptor acceptor{ioc, {tcp::v4(), request_manager_port}};

    // asynchronous accept loop
    auto do_accept = [&](auto&& self) -> void {
        acceptor.async_accept([&](beast::error_code ec, tcp::socket socket) {
            if (!ec)
                std::make_shared<HttpSession>(std::move(socket), ioc)->run();
            self(self);
        });
    };

    do_accept(do_accept); // Start retrieving accept
    std::thread io_thread([&ioc]()
    {
        SPDLOG_LOGGER_INFO(Logger::instance(), "The server starts at http://localhost:" + std::to_string(request_manager_port));
        ioc.run();
    });

    io_thread.join();
    return 0;
}
