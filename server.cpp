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
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <boost/asio.hpp>
#include <boost/asio/dispatch.hpp>
#include <boost/asio/strand.hpp>
#include <boost/config.hpp>
#include <boost/process.hpp>
#include <filesystem>
#include <fstream>
#include <thread>
#include <iostream>
#include <queue>
#include <nlohmann/json.hpp>

#include "settings/sim_server.hpp"
#include "types/sim_server.hpp"
#include "utils/Logger.hpp"
#include "utils/common.hpp"

namespace beast = boost::beast;
namespace http  = beast::http;
namespace net   = boost::asio;
namespace bp    = boost::process;
namespace fs    = std::filesystem;
using     tcp   = net::ip::tcp;
using     json  = nlohmann::json;

void signal_handler(int signal) {
    SPDLOG_LOGGER_INFO(Logger::instance(), "Received signal {}, unmounting NFS", signal);
    safe_system(unmount_nfs_command());
    std::signal(signal, SIG_DFL);
    std::raise(signal);
}

void cleanup_on_exit() {
    SPDLOG_LOGGER_INFO(Logger::instance(), "Program exiting normally, unmounting NFS");
    safe_system(unmount_nfs_command());
}

void run_simulator(
    net::io_context& ioc,
    net::strand<net::io_context::executor_type>& strand,
    const SimulationTask& task,
    std::function<void(int)> on_complete)
{
    std::string command = simulator_exec_command(task.simulator, task.version, task.inputfile, task.outputfile);
    SPDLOG_LOGGER_INFO(Logger::instance(), "Start Simulation: {}", command);

    // Used to maintain the lifecycle of bp::child objects
    static std::unordered_map<std::string, std::shared_ptr<bp::child>> active_processes;

    // Launch process asynchronously
    auto process = std::make_shared<bp::child>
    (
        command,
        bp::std_out > stdout,
        bp::on_exit = [strand, command, task, on_complete](int exit_code, const std::error_code& ec)
        {
            active_processes.erase(command);  // Clear
            net::post(strand, [exit_code, ec, task, on_complete]
            {
                if (ec)
                {
                    SPDLOG_LOGGER_ERROR(Logger::instance(), "Simulator {} failed to execute: {}", task.case_id, ec.message());
                    on_complete(-1);
                    return;
                }
                SPDLOG_LOGGER_INFO(Logger::instance(), "{} Execution completed, code = {}", task.case_id, exit_code);
                on_complete(exit_code == 0 ? 0 : -1);
            });
        },
        ioc
    );

    active_processes[command] = process;
}

class Session : public std::enable_shared_from_this<Session>
{
public:
    Session(tcp::socket socket, net::io_context& ioc)
    : ioc_(ioc),
      stream_(std::move(socket)),
      callback_stream_(ioc),
      resolver_(ioc),
      strand_(net::make_strand(ioc)),
      callback_strand_(net::make_strand(ioc))
    {
        auto remote_endpoint = stream_.socket().remote_endpoint();
        SPDLOG_LOGGER_INFO(Logger::instance(), "Get Connection: IP: {}, port: {}",
                           remote_endpoint.address().to_string(), remote_endpoint.port());
    }

    void run()
    {
        // Upon initial connection, once the connection is successful, the system begins reading client requests.
        connect_callback(strand_, [self = shared_from_this()]
        {
            if (!self->is_reading_) {
                self->is_reading_ = true;
                self->read_request();
            }
        });
    }

private:
    net::io_context& ioc_;
    beast::tcp_stream stream_; // client
    beast::tcp_stream callback_stream_;
    tcp::resolver resolver_;
    net::strand<net::io_context::executor_type> strand_; // For request handling
    net::strand<net::io_context::executor_type> callback_strand_; // For callbacks
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;
    bool callback_connected_ = false;
    std::queue<json> pending_callbacks_; // Store callback data to be sent
    bool is_reading_ = false; // Tracking whether client requests are being read

    struct SingleEndpointConnectHandler
    {
        std::shared_ptr<Session> self;
        std::function<void()> on_connect;

        // Use void(beast::error_code) to match the static assertion
        void operator()(beast::error_code ec)
        {
            if (ec)
            {
                SPDLOG_LOGGER_ERROR(Logger::instance(), "Connect to callback {}:{} failed: {}",
                                    request_manager_ip, request_manager_port, ec.message());
                self->callback_connected_ = false;
                self->close_callback();
                return;
            }
            SPDLOG_LOGGER_INFO(Logger::instance(), "Connected to callback {}:{}", request_manager_ip, request_manager_port);
            self->callback_connected_ = true;
            on_connect();
        }
    };

    void connect_callback(net::strand<net::io_context::executor_type> on_connect_strand, std::function<void()> on_connect)
    {
        resolver_.async_resolve(request_manager_ip, request_manager_port,
            net::bind_executor(callback_strand_,
            [self = shared_from_this(), on_connect_strand, on_connect]
            (beast::error_code ec, tcp::resolver::results_type results)
            {
                if (ec)
                {
                    SPDLOG_LOGGER_ERROR(Logger::instance(), "Resolve callback address {}:{} failed: {}",
                                        request_manager_ip, request_manager_port, ec.message());
                    self->callback_connected_ = false;
                    return;
                }
                if (results.empty())
                {
                    SPDLOG_LOGGER_ERROR(Logger::instance(), "No endpoints resolved for {}:{}", request_manager_ip, request_manager_port);
                    self->callback_connected_ = false;
                    return;
                }
                // Use single-endpoint async_connect with simplified handler
                self->callback_stream_.async_connect(*results.begin(),
                    net::bind_executor(on_connect_strand, SingleEndpointConnectHandler{self, on_connect}));
            }));
    }

    void read_request()
    {
        req_ = {};
        buffer_.consume(buffer_.size());
        http::async_read(stream_, buffer_, req_,
            net::bind_executor(strand_, [self = shared_from_this()](beast::error_code ec, std::size_t bytes_transferred)
            {
                self->is_reading_ = false; // Read complete, reset flag.
                if (ec == beast::http::error::end_of_stream || ec == net::error::eof)
                {
                    SPDLOG_LOGGER_WARN(Logger::instance(), "Client closed connection");
                    self->close_client();
                    return;
                }
                if (ec == net::error::connection_reset || ec == net::error::connection_aborted)
                {
                    SPDLOG_LOGGER_ERROR(Logger::instance(), "Client connection error: {}", ec.message());
                    self->close_client();
                    return;
                }
                if (ec)
                {
                    SPDLOG_LOGGER_ERROR(Logger::instance(), "async_read failed: {}", ec.message());
                    self->close_client();
                    return;
                }
                SPDLOG_LOGGER_INFO(Logger::instance(), "Got request: {} {}, bytes: {}",
                                   std::string(self->req_.method_string()), std::string(self->req_.target()), bytes_transferred);
                SPDLOG_LOGGER_INFO(Logger::instance(), "body: {}", self->req_.body());
                SPDLOG_LOGGER_INFO(Logger::instance(), "keep alive: {}", self->req_.keep_alive());
                self->handle_request();
            }));
    }

    void handle_request()
    {
        if (req_.method() == http::verb::post && req_.target() == sim_server_target)
        {
            // Parse JSON body
            SimulationTask task;
            try
            {
                json j = json::parse(req_.body());
                task = j.get<SimulationTask>();
            }
            catch (const std::exception& e)
            {
                SPDLOG_LOGGER_ERROR(Logger::instance(), "JSON parse error: {}", e.what());
                auto res = std::make_shared<http::response<http::string_body>>(http::status::bad_request, req_.version());
                res->set(http::field::content_type, "application/json");
                res->keep_alive(req_.keep_alive());
                res->body() = error_response_body("Invalid JSON request body");
                res->prepare_payload();
                write_response(res);
                return;
            }

            if (!check_simulator_exist(task.simulator, task.version))
            {
                SPDLOG_LOGGER_ERROR(Logger::instance(), "Simulator NOT exist: {}/{}", task.simulator, task.version);
                auto res = std::make_shared<http::response<http::string_body>>(http::status::bad_request, req_.version());
                res->set(http::field::content_type, "application/json");
                res->keep_alive(req_.keep_alive());
                res->body() = error_response_body("Simulator NOT exist");
                res->prepare_payload();
                write_response(res);
                return;
            }

            // Respond to the client immediately
            auto res = std::make_shared<http::response<http::string_body>>(http::status::ok, req_.version());
            res->set(http::field::content_type, "application/json");
            res->keep_alive(req_.keep_alive());
            res->body() = message_response_body("Request received");
            res->prepare_payload();
            write_response(res);

            // Submit external program to execute task
            handle_new_task(task);
        }
        else
        {
            // Handling unsupported requests
            auto res = std::make_shared<http::response<http::string_body>>(http::status::bad_request, req_.version());
            res->set(http::field::content_type, "application/json");
            res->keep_alive(req_.keep_alive());
            res->body() = error_response_body("Invalid request");
            res->prepare_payload();
            write_response(res);
        }
    }

    void write_response(std::shared_ptr<http::response<http::string_body>> res) {
        http::async_write(stream_, *res,
            net::bind_executor(strand_, [self = shared_from_this(), res](beast::error_code ec, std::size_t) {
                if (ec == net::error::connection_reset || ec == net::error::connection_aborted) {
                    SPDLOG_LOGGER_ERROR(Logger::instance(), "Client connection error: {}", ec.message());
                    self->close_client();
                    return;
                }
                if (ec) {
                    SPDLOG_LOGGER_ERROR(Logger::instance(), "async_write failed: {}", ec.message());
                    self->close_client();
                    return;
                }
                SPDLOG_LOGGER_INFO(Logger::instance(), "Response sent");
                if (res->keep_alive() && !self->is_reading_) {
                    self->is_reading_ = true;
                    self->read_request();
                } else if (!res->keep_alive()) {
                    self->close_client();
                }
            }));
    }

    void handle_new_task(const SimulationTask& task)
    {
        run_simulator(ioc_, callback_strand_, task, [self = shared_from_this(), task](int code) {
            json sim_result = SimulationResult
            {
                task.simulator,
                task.version,
                task.app_id,
                task.case_id,
                output_filename,
                code == 0
            };
            self->send_callback(sim_result);
        });
    }

    void send_callback(const json& response_body) {
        if (callback_connected_)
        {
            write_callback(response_body);
        }
        // else
        // {
        //     SPDLOG_LOGGER_WARN(Logger::instance(), "Callback stream not connected, queuing and reconnecting");
        //     pending_callbacks_.push(response_body); // Store pending callbacks
        //     reconnect_callback_and_write();
        // }
    }

    void reconnect_callback_and_write()
    {
        connect_callback(callback_strand_, [self = shared_from_this()] { self->pop_callback_response_and_write(); });
    }

    void pop_callback_response_and_write()
    {
        if (!pending_callbacks_.empty())
        {
            auto response_body = std::move(pending_callbacks_.front());
            pending_callbacks_.pop();
            write_callback(response_body);
        }
    }

    void write_callback(const json& response_body)
    {
        auto req = std::make_shared<http::request<http::string_body>>(http::verb::post, request_manager_target, 11);
        req->set(http::field::host, request_manager_ip);
        req->set(http::field::content_type, "application/json");
        req->keep_alive(true);
        req->body() = response_body.dump();
        req->prepare_payload();

        SPDLOG_LOGGER_INFO(Logger::instance(), "Sending callback POST to {}:{}", request_manager_ip, request_manager_port);
        http::async_write(callback_stream_, *req,
            net::bind_executor(callback_strand_, [self = shared_from_this(), req](beast::error_code ec, std::size_t)
            {
                if (ec == beast::http::error::end_of_stream || ec == net::error::eof)
                {
                    SPDLOG_LOGGER_WARN(Logger::instance(), "Callback connection closed by server");
                    self->callback_connected_ = false;
                    self->close_callback();
                    self->pending_callbacks_.push(json::parse(req->body()));
                    // self->reconnect_callback_and_write();
                    return;
                }
                if (ec == net::error::connection_reset || ec == net::error::connection_aborted)
                {
                    SPDLOG_LOGGER_ERROR(Logger::instance(), "Callback connection error: {}", ec.message());
                    self->callback_connected_ = false;
                    self->close_callback();
                    self->pending_callbacks_.push(json::parse(req->body()));
                    // self->reconnect_callback_and_write();
                    return;
                }
                if (ec)
                {
                    SPDLOG_LOGGER_ERROR(Logger::instance(), "Callback async_write failed: {}", ec.message());
                    self->callback_connected_ = false;
                    self->close_callback();
                    self->pending_callbacks_.push(json::parse(req->body()));
                    // self->reconnect_callback_and_write();
                    return;
                }
                SPDLOG_LOGGER_INFO(Logger::instance(), "Callback POST sent to {}:{}", request_manager_ip, request_manager_port);
                self->read_callback();
            }));
    }

    void read_callback()
    {
        auto buffer = std::make_shared<beast::flat_buffer>();
        auto res = std::make_shared<http::response<http::string_body>>();
        http::async_read(callback_stream_, *buffer, *res,
            net::bind_executor(callback_strand_, [self = shared_from_this(), res, buffer](beast::error_code ec, std::size_t)
            {
                if (ec == beast::http::error::end_of_stream || ec == net::error::eof)
                {
                    SPDLOG_LOGGER_WARN(Logger::instance(), "Callback connection closed by server");
                    self->callback_connected_ = false;
                    self->close_callback();
                    // if (!self->pending_callbacks_.empty())
                    //     self->reconnect_callback_and_write();
                    return;
                }
                if (ec == net::error::connection_reset || ec == net::error::connection_aborted)
                {
                    SPDLOG_LOGGER_ERROR(Logger::instance(), "Callback connection error: {}", ec.message());
                    self->callback_connected_ = false;
                    self->close_callback();
                    // if (!self->pending_callbacks_.empty())
                    //     self->reconnect_callback_and_write();
                    return;
                }
                if (ec)
                {
                    SPDLOG_LOGGER_ERROR(Logger::instance(), "Callback async_read failed: {}", ec.message());
                    std::string rawData(boost::asio::buffer_cast<const char *>(buffer->data()), boost::asio::buffer_size(buffer->data()));
                    std::cout << "Raw request: " << rawData << std::endl;
                    self->callback_connected_ = false;
                    self->close_callback();
                    // if (!self->pending_callbacks_.empty())
                    //     self->reconnect_callback_and_write();
                    return;
                }
                SPDLOG_LOGGER_INFO(Logger::instance(), "Callback response: code = {}, body = {}", res->result_int(), res->body());
                if (!res->keep_alive())
                {
                    SPDLOG_LOGGER_INFO(Logger::instance(), "Callback connection not kept alive, closing");
                    self->callback_connected_ = false;
                    self->close_callback();
                    // if (!self->pending_callbacks_.empty())
                    //     self->reconnect_callback_and_write();
                }
                // else
                // {
                //     self->pop_callback_response_and_write();
                // }
            }));
    }

    void close_client()
    {
        beast::error_code ec;
        stream_.socket().shutdown(tcp::socket::shutdown_both, ec);
        if (ec && ec != net::error::not_connected)
            SPDLOG_LOGGER_ERROR(Logger::instance(), "Client shutdown error: {}", ec.message());
        stream_.socket().close(ec);
        if (ec && ec != net::error::not_connected)
            SPDLOG_LOGGER_ERROR(Logger::instance(), "Client close error: {}", ec.message());
        SPDLOG_LOGGER_INFO(Logger::instance(), "Client connection closed");
    }

    void close_callback()
    {
        beast::error_code ec;
        callback_stream_.socket().shutdown(tcp::socket::shutdown_both, ec);
        if (ec && ec != net::error::not_connected)
            SPDLOG_LOGGER_ERROR(Logger::instance(), "Callback shutdown error: {}", ec.message());
        callback_stream_.socket().close(ec);
        if (ec && ec != net::error::not_connected)
            SPDLOG_LOGGER_ERROR(Logger::instance(), "Callback close error: {}", ec.message());
        SPDLOG_LOGGER_INFO(Logger::instance(), "Callback connection closed");
    }
};

// Server: Listen to connection
class Server
{
public:
    Server(net::io_context& ioc, uint16_t port)
    : ioc_(ioc), acceptor_(ioc, tcp::endpoint(tcp::v4(), port)), work_(net::make_work_guard(ioc)) {}

    void run() { accept(); }

private:
    net::io_context &ioc_;
    tcp::acceptor acceptor_;
    net::executor_work_guard<net::io_context::executor_type> work_;

    void accept()
    {
        acceptor_.async_accept(
            [this](beast::error_code ec, tcp::socket socket)
            {
                if (!ec)
                {
                    SPDLOG_LOGGER_INFO(Logger::instance(), "Accepted new connection");
                    std::make_shared<Session>(std::move(socket), ioc_)->run();
                }
                else
                {
                    SPDLOG_LOGGER_ERROR(Logger::instance(), "Accept failed: {}", ec.message());
                }
                accept();
            });
    }
};

int main(int argc, char *argv[])
{
    // Init logger
    auto cfg = Logger::parse_cli_args(argc, argv);
    Logger::init(cfg);
    SPDLOG_LOGGER_INFO(Logger::instance(), "Logger Loads Successfully!");

    SPDLOG_LOGGER_INFO(Logger::instance(), "Mount NFS");
    SPDLOG_LOGGER_INFO(Logger::instance(), mount_nfs_command());
    int code = safe_system(mount_nfs_command());
    if (code != 0)
    {
        SPDLOG_LOGGER_CRITICAL(Logger::instance(), "Mount NFS Failed");
        return EXIT_FAILURE;
    }

    // Registered signal processing
    std::signal(SIGINT, signal_handler);  // Ctrl+C
    std::signal(SIGTERM, signal_handler); // Termination signal
    // Normal registration, exit, and cleanup
    std::atexit(cleanup_on_exit);

    try
    {
        net::io_context ioc;

        // Start listening to port
        auto server = std::make_shared<Server>(ioc, sim_server_port);
        server->run();

        // TODO: Try 1~3
        // Run io_context with multi threads
        // int thread_count = std::max(2, static_cast<int>(std::thread::hardware_concurrency()));
        int thread_count = 1;
        std::vector<std::thread> threads;
        for (int i = 0; i < thread_count; ++i)
            threads.emplace_back([&ioc]()
            {
                try {
                    ioc.run();
                } catch(const std::exception& e) {
                    SPDLOG_LOGGER_ERROR(Logger::instance(), "IO thread exception: {}", e.what());
                }
            });

        SPDLOG_LOGGER_INFO(Logger::instance(), "Server started at http://localhost:{}", sim_server_port);

        // Block main threads until all threads done.
        for (auto& t : threads)
            t.join();
    }
    catch (const std::exception& e)
    {
        SPDLOG_LOGGER_ERROR(Logger::instance(), "Main exception: {}", e.what());
    }

    return EXIT_SUCCESS;
}