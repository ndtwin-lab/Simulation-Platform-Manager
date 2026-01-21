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

    #include <algorithm>
    #include <boost/asio.hpp>
    #include <boost/asio/connect.hpp>
    #include <boost/asio/dispatch.hpp>
    #include <boost/asio/ip/tcp.hpp>
    #include <boost/asio/strand.hpp>
    #include <boost/beast/core.hpp>
    #include <boost/beast/http.hpp>
    #include <boost/beast/version.hpp>
    #include <boost/config.hpp>
    #include <boost/stacktrace.hpp>
    #include <filesystem>
    #include <fstream>
    #include <iostream>
    #include <nlohmann/json.hpp>
    #include <random>
    #include <string>
    #include <thread>

    #include "settings/app.hpp"
    #include "types/app.hpp"
    #include "utils/Logger.hpp"
    #include "utils/common.hpp"

    namespace beast = boost::beast;
    namespace http = beast::http;
    namespace net = boost::asio;
    namespace fs = std::filesystem;
    using tcp = net::ip::tcp;
    using json = nlohmann::json;

    class ExitHandler
    {
    public:
        ~ExitHandler()
        {
            safe_system(unmount_nfs_command());
        }
    };

    static ExitHandler handler;

    static inline std::string to_lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    
    void post_requests(const int num_cases, net::io_context &ioc)
    {
        // TODO
        const std::string& template_path = "simple_sim_input.txt";
        // TODO
        const std::string simulator = "simple_sim";  // or "power_sim"
        const std::string version   = "1.0";

        // Detect extension (.json or .txt)
        const std::string ext = to_lower(fs::path(template_path).extension().string());
        const bool is_json = (ext == ".json");
        // const bool is_txt  = (ext == ".txt");

        // Optional: warn if the output file name’s extension doesn’t match template’s
        // const std::string out_ext = to_lower(fs::path(input_filename).extension().string());
        // if (!out_ext.empty() && out_ext != ext) {
        //     SPDLOG_LOGGER_WARN(Logger::instance(),
        //         "Template '{}' (ext {}) does not match input_filename '{}' (ext {}).",
        //         template_path, ext, input_filename, out_ext);
        // }

        // Load the template
        std::string text_template;
        json json_template;

        if (is_json) {
            std::ifstream jf(template_path);
            if (!jf) {
                SPDLOG_LOGGER_ERROR(Logger::instance(), "Unable to open input file(JSON): {}", template_path);
                return;
            }
            try {
                jf >> json_template;
            } catch (const std::exception& e) {
                SPDLOG_LOGGER_ERROR(Logger::instance(), "Parse JSON failed: {} -> {}", template_path, e.what());
                return;
            }
        } else {
            // Treat anything non-.json as raw text (.txt or otherwise)
            std::ifstream tf(template_path, std::ios::binary);
            if (!tf) {
                SPDLOG_LOGGER_ERROR(Logger::instance(), "Unable to open input file(TEXT): {}", template_path);
                return;
            }
            text_template.assign((std::istreambuf_iterator<char>(tf)), std::istreambuf_iterator<char>());
        }

        // Connect once (we’ll reconnect if server closes)
        tcp::resolver resolver(ioc);
        beast::tcp_stream stream(ioc);
        auto const results = resolver.resolve(request_manager_ip, request_manager_port);

        boost::system::error_code ec;
        stream.connect(results, ec);
        if (ec) {
            SPDLOG_LOGGER_CRITICAL(Logger::instance(), "Failed to connect to {}:{}: {}",
                                request_manager_ip, request_manager_port, ec.message());
            return;
        }

        for (int i = 1; i <= num_cases; ++i)
        {
            std::string case_id = "case" + std::to_string(i);

            // Where each case’s input file should go
            fs::path input_file_path = abs_input_file_path(simulator, version, case_id, input_filename);

            // Ensure directory exists
            std::error_code fec;
            fs::create_directories(input_file_path.parent_path(), fec);
            if (fec) {
                SPDLOG_LOGGER_ERROR(Logger::instance(), "Failed to create folder: {} -> {}",
                                    input_file_path.parent_path().string(), fec.message());
                return;
            }

            // Write per-case input file
            {
                std::ofstream out(input_file_path, std::ios::binary | std::ios::trunc);
                if (!out) {
                    SPDLOG_LOGGER_ERROR(Logger::instance(), "Unable to write: {}", input_file_path.string());
                    return;
                }
                if (is_json) {
                    // If you need to customize JSON per case, modify json_template here
                    out << json_template.dump(2) << '\n';
                } else {
                    out << text_template; // raw text for .txt (e.g., "10 20")
                }
                out.close();
                SPDLOG_LOGGER_INFO(Logger::instance(), "Generate {}", input_file_path.string());
            }

            // Build HTTP request to the request manager
            http::request<http::string_body> req{http::verb::post, request_manager_target_for_app, 11};
            req.set(http::field::host, request_manager_ip);
            req.keep_alive(true);
            req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
            req.set(http::field::content_type, "application/json");

            // Body: SimulationRequest {simulator, version, app_id, case_id, input_filename}
            json body_json = SimulationRequest{simulator, version, app_id, case_id, input_filename};
            req.body() = body_json.dump();
            req.prepare_payload();

            // Send
            http::write(stream, req);
            SPDLOG_LOGGER_INFO(Logger::instance(), "{} to {}", std::string(req.method_string()), request_manager_target_for_app);
            SPDLOG_LOGGER_INFO(Logger::instance(), "body = {}", req.body());

            // Receive
            beast::flat_buffer buffer;
            http::response<http::string_body> res;
            boost::system::error_code rec;
            http::read(stream, buffer, res, rec);

            if (rec == http::error::end_of_stream || !res.keep_alive()) {
                SPDLOG_LOGGER_WARN(Logger::instance(), "Server closed connection after response; reconnecting…");
                boost::system::error_code sec;
                stream.socket().shutdown(tcp::socket::shutdown_both, sec);
                stream.socket().close(sec);
                stream.connect(results, ec);
                if (ec) {
                    SPDLOG_LOGGER_CRITICAL(Logger::instance(), "Reconnect failed: {}", ec.message());
                    return;
                }
            } else if (rec) {
                throw beast::system_error{rec};
            }

            SPDLOG_LOGGER_INFO(Logger::instance(), "Response: code = {}", res.result_int());
            SPDLOG_LOGGER_INFO(Logger::instance(), "body = {}", res.body());
            SPDLOG_LOGGER_INFO(Logger::instance(), "keep alive = {}", res.keep_alive());
        }
    }

    class HttpSession : public std::enable_shared_from_this<HttpSession>
    {
    public:
        explicit HttpSession(tcp::socket socket) : _stream(std::move(socket))
        {
        }

        ~HttpSession()
        {
            _stream.close();
            _stream.close();
        }

        void run()
        {
            do_read();
        }

    private:
        beast::tcp_stream _stream;
        beast::flat_buffer _buffer;
        http::request<http::string_body> _req;

        void do_read()
        {
            _req = {}; // reset
            _buffer.consume(_buffer.size());

            auto self = shared_from_this();
            http::async_read(_stream, _buffer, _req, [self](beast::error_code ec, std::size_t) {
                if (ec == beast::http::error::end_of_stream || ec == net::error::eof)
                {
                    SPDLOG_LOGGER_WARN(Logger::instance(), "Client closed connection");
                    beast::error_code ec;
                    self->_stream.socket().shutdown(tcp::socket::shutdown_both, ec);
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
            SPDLOG_LOGGER_INFO(Logger::instance(), "Got request: {}", std::string(_req.method_string()));
            SPDLOG_LOGGER_INFO(Logger::instance(), "body: {}", _req.body());

            if (_req.method() != http::verb::post || _req.target() != app_target)
            {
                SPDLOG_LOGGER_ERROR(Logger::instance(), "Unsupported method or path: {}, {}",
                                    std::string(_req.method_string()), std::string(_req.target()));
                return;
            }

            try
            {
                auto res = std::make_shared<http::response<http::string_body>>(http::status::ok, _req.version());
                res->set(http::field::content_type, "text/plain");
                res->set(http::field::connection, "keep-alive");
                res->body() = "Received Result\n";
                res->prepare_payload();

                auto self = shared_from_this();
                http::async_write(_stream, *res, [self, res](beast::error_code ec, std::size_t) {
                    if (ec)
                    {
                        SPDLOG_LOGGER_ERROR(Logger::instance(), "async_write failed: {}", ec.message());
                        return;
                    }

                    SPDLOG_LOGGER_INFO(Logger::instance(), "Wait for next request...");
                    self->do_read(); // Go back to reading the next stroke
                });
            }
            catch (std::exception &e)
            {
                SPDLOG_LOGGER_ERROR(Logger::instance(), "handle request failed: {}", e.what());
                do_read(); // Go back to reading the next stroke
            }
        }
    };

    int preInstall();
    int main(int argc, char *argv[])
    {
        auto cfg = Logger::parse_cli_args(argc, argv);
        Logger::init(cfg);
        SPDLOG_LOGGER_INFO(Logger::instance(), "Logger Loads Successfully!");

        preInstall();

        SPDLOG_LOGGER_INFO(Logger::instance(), "Get App Id {}", app_id);
        SPDLOG_LOGGER_INFO(Logger::instance(), "Mount NFS");
        SPDLOG_LOGGER_INFO(Logger::instance(), mount_nfs_command(app_id));
        int code = safe_system(mount_nfs_command(app_id));
        if (code != 0)
        {
            SPDLOG_LOGGER_CRITICAL(Logger::instance(), "Mount NFS Failed");
            return -1;
        }

        net::io_context ioc;
        auto work = net::make_work_guard(ioc); // Prevent io_context from exiting prematurely
        tcp::acceptor acceptor{ioc, {tcp::v4(), app_port}};

        // asynchronous accept loop
        auto do_accept = [&](auto &&self) -> void {
            acceptor.async_accept([&](beast::error_code ec, tcp::socket socket) {
                if (!ec)
                    std::make_shared<HttpSession>(std::move(socket))->run();
                self(self);
            });
        };

        do_accept(do_accept); // Start retrieving accept

        std::vector<std::thread> threads;
        // int thread_count = std::thread::hardware_concurrency();
        int thread_count = 1;
        for (int i = 0; i < thread_count; ++i)
            threads.emplace_back([&ioc]() { ioc.run(); });

        SPDLOG_LOGGER_INFO(Logger::instance(), "The server starts at http://localhost:" + std::to_string(app_port));

        post_requests(1, ioc);

        for (auto &t : threads)
            t.join();
        return 0;
    }

    int preInstall()
    {
        try
        {
            // === JSON Body ===
            json json_body =
                json{{"app_name", "power"},
                    {"simulation_completed_url", "http://" + app_ip + ":" + std::to_string(app_port) + app_target}};

            // Set up IO context
            net::io_context ioc;

            // Resolve the server address
            tcp::resolver resolver(ioc);
            auto const results = resolver.resolve(ndt_ip, ndt_port);

            // Connect to server
            tcp::socket socket(ioc);
            net::connect(socket, results.begin(), results.end());

            // === Create HTTP POST request ===
            http::request<http::string_body> req{http::verb::post, ndt_target, 11};
            req.set(http::field::host, ndt_ip);
            req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
            req.set(http::field::content_type, "application/json");
            req.body() = json_body.dump();
            req.prepare_payload();

            // Send HTTP request
            http::write(socket, req);

            // Buffer for response
            beast::flat_buffer buffer;

            // Container for response
            http::response<http::string_body> res;

            // Receive HTTP response
            http::read(socket, buffer, res);

            // === Print the HTTP response ===
            std::cout << res << std::endl;

            auto j = json::parse(res.body());
            app_id = std::to_string(j.at("app_id").get<int>());
            // Gracefully close the socket
            beast::error_code ec;
            socket.shutdown(tcp::socket::shutdown_both, ec);
            if (ec && ec != beast::errc::not_connected)
                throw beast::system_error{ec};
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error: " << e.what() << std::endl;
            return 1;
        }

        return 0;
    }