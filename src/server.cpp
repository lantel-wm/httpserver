#include "bytes_buffer.hpp"
#include "http_parser.hpp"
#include "http_writer.hpp"
#include "io_context.hpp"
#include "utils.hpp"
#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <exception>
#include <fcntl.h>
#include <fmt/core.h>
#include <fmt/format.h>
#include <functional>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>

template <typename... Args> using callback = std::function<void(Args...)>;

struct async_file {
  int m_fd;

  static async_file async_warp(int fd) {
    int flags = CHECK_CALL(fcntl, fd, F_GETFL);
    flags |= O_NONBLOCK; // set file to non-block
    CHECK_CALL(fcntl, fd, F_SETFL, flags);
    return async_file{fd};
  }

  ssize_t sync_read(bytes_view buf) {
    ssize_t ret;
    do {
      ret = CHECK_CALL_EXCEPT(EAGAIN, read, m_fd, buf.data(), buf.size());
    } while (ret == -1);
    return ret;
  }

  void async_read(bytes_view buf, callback<ssize_t> cb) {
    ssize_t ret;
    do {
      ret = CHECK_CALL_EXCEPT(EAGAIN, read, m_fd, buf.data(), buf.size());
    } while (ret == -1);
    cb(ret); // callback function excecuted after read is done
  }

  ssize_t sync_write(bytes_view buf) {
    return CHECK_CALL_EXCEPT(EPIPE, write, m_fd, buf.data(), buf.size());
  }

  size_t sync_write(std::string_view buf) {
    return CHECK_CALL_EXCEPT(EPIPE, write, m_fd, buf.data(), buf.size());
  }
};

std::vector<std::thread> pool;

struct http_connection_handler {
  async_file m_conn;
  bytes_buffer m_buf{1024};
  http_request_parser<> m_req_parser;

  void do_init(int connfd) { m_conn = async_file::async_warp(connfd); }

  void do_read() {
    fmt::print("Start reading...\n");
    m_conn.async_read(m_buf, [this](size_t n) {
      if (n == 0) {
        fmt::print("Connection terminated due to EOF: {}\n", m_conn.m_fd);
        do_close();
        return;
      }
      fmt::print("Read {} bytes: {}\n", n, std::string_view(m_buf.data(), n));
      m_req_parser.push_chunk(m_buf.subspan(0, n));
      if (!m_req_parser.request_finished()) {
        do_read();
      } else {
        do_write();
      }
    });
  }

  void do_write() {
    std::string header = m_req_parser.headers_raw();
    std::string body = m_req_parser.body();
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
    bytes_view out_buffer = res_writer.buffer();
    m_conn.sync_write(out_buffer);
    m_conn.sync_write(body);
    fmt::print("Responding: {}\n", connid);
    do_read(); // keep-alive
  }

  void do_close() { close(m_conn.m_fd); }
};

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
      http_connection_handler conn_handler;
      conn_handler.do_init(connid);
      while (true) {
        // char in_buffer[1024];
        bytes_buffer in_buffer(1024);
        _http_parser_base req_parse;
        do {
          conn.async_read(in_buffer, [](size_t n) {
            // if EOF is read, close the connection
            if (n == 0) {
              close(connid);
              fmt::print("Connection terminated due to EOF: {}\n", connid);
              return;
            }
            req_parse.push_chunk(in_buffer);
          });
        } while (req_parse.request_finished());
        fmt::print("Request received: {}\n", connid);
      }
      close(connid);
      fmt::print("Connection done: {}\n", connid);
    });
  }
  for (auto &t : pool) {
    t.join();
  }
}

int main() {
  try {
    server();
  } catch (const std::exception &e) {
    fmt::print("Error: {}\n", e.what());
  };
  return 0;
}
