#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fmt/format.h>
#include <string>
#include <sys/types.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include "bytes_buffer.hpp"
#include "http_parser.hpp"
#include "http_writer.hpp"
#include "io_context.hpp"
#include "utils.hpp"

std::vector<std::thread> pool;

void server() {
    std::string port = "8080";
    fmt::print("Listening 127.0.0.1:{}\n", port);
    address_resolver resolver;
    resolver.resolve("127.0.0.1", port);
    auto info = resolver.get_first_entry();
    int listenfd = info.create_socket_and_bind();
    CHECK_CALL(listen, listenfd, SOMAXCONN);
    address_resolver::address addr;
    while (true) {
        int connid = CHECK_CALL(accept, listenfd, &addr.m_addr, &addr.m_addrlen);
        pool.emplace_back([connid] {
            while (true) {
                // char in_buffer[1024];
                bytes_buffer in_buffer(1024);
                _http_parser_base req_parse;
                do {
                    size_t n = CHECK_CALL_EXCEPT(ECONNRESET, read, connid, in_buffer.data(), in_buffer.size());
                    // if EOF is read, close the connection
                    if (n == 0) {
                        close(connid);
                        fmt::print("Connection terminated due to EOF: {}\n", connid);
                        return;
                    } else if (n == static_cast<size_t>(-1)) { // ECONNRESET caught
                        close(connid);
                        fmt::print("Connection terninated by client: {}\n", connid);
                        return;
                    }
                    req_parse.push_chunk(in_buffer);
                } while (req_parse.request_finished());
                fmt::print("Request received: {}\n", connid);
                std::string header = req_parse.headers_raw();
                std::string body = req_parse.body();
                // fmt::print("Request header: {}\n", header);
                // fmt::print("Request body: {}\n", body);

                if (body.empty()) {
                    body = "<font color=\"red\"><b>请求为空</b></font>";
                } else {
                    body = "<font color=\"red\"><b>你的请求是: [" + body + "]</b></font>";
                }

                http_response_writer res_writer;
                res_writer.begin_header(200);
                res_writer.write_header("Server", "cpp_http");
                res_writer.write_header("Content-type", "text/html;charset=utf-8");
                res_writer.write_header("Connecetion", "keep-alive");
                res_writer.write_header("Content-length", std::to_string(body.size()));
                res_writer.end_header(); // "\r\n\r\n"
                std::string& out_buffer = res_writer.buffer();
                if (CHECK_CALL_EXCEPT(EPIPE, write, connid, out_buffer.data(), out_buffer.size()) == -1)
                    break; // EPIPE caught
                if (CHECK_CALL_EXCEPT(EPIPE, write, connid, body.data(), body.size()) == -1)
                    break; // EPIPE caught

                // fmt::print("Response header: {}\n", out_buffer.data());
                // fmt::print("Response body: {}\n", body.data());
                fmt::print("Responding: {}\n", connid);
            }
            close(connid);
            fmt::print("Connection done: {}\n", connid);

        });
    }
    for (auto& t: pool) {
        t.join();
    }
}

int main() {
    try {
        server();
    } catch (const std::exception& e) {
        fmt::print("Error: {}\n", e.what());
    };
    return 0;
}