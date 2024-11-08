#include "bytes_buffer.hpp"
#include "callback.hpp"
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
#include <fcntl.h>
#include <fmt/core.h>
#include <string>
#include <string_view>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

int epollfd;

struct async_file {
  int m_fd;

  static async_file async_warp(int fd) {
    int flags = CHECK_CALL(fcntl, fd, F_GETFL);
    flags |= O_NONBLOCK; // set file to non-block
    CHECK_CALL(fcntl, fd, F_SETFL, flags);

    struct epoll_event event;
    event.events = EPOLLET;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);

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
    ret = CHECK_CALL_EXCEPT(EAGAIN, read, m_fd, buf.data(), buf.size());
    if (ret != -1) { // EAGAIN
      cb(ret);
      return;
    }

    callback<> resume = [this, buf, cb = std::move(cb)]() mutable {
      async_read(buf, std::move(cb));
    };

    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET;
    event.data.ptr = resume.leak_address();
    epoll_ctl(epollfd, EPOLL_CTL_MOD, m_fd, &event);
  }

  ssize_t sync_write(bytes_view buf) {
    return CHECK_CALL_EXCEPT(EPIPE, write, m_fd, buf.data(), buf.size());
  }

  size_t sync_write(std::string_view buf) {
    return CHECK_CALL_EXCEPT(EPIPE, write, m_fd, buf.data(), buf.size());
  }

  int sync_accept(struct sockaddr *addr, socklen_t *addrlen) {
    int connid = CHECK_CALL(accept, m_fd, addr, addrlen);
    fmt::print("Accept a conncetion: {}\n", connid);
    return connid;
  }

  void async_accept(struct sockaddr *addr, socklen_t *addrlen, callback<> cb) {}

  // void async_accept

  void close_file() {
    epoll_ctl(epollfd, EPOLL_CTL_DEL, m_fd, nullptr);
    close(m_fd);
  }
};

struct http_connection_handler {
  async_file m_conn;
  bytes_buffer m_buf{1024};
  http_request_parser<> m_req_parser;

  void do_init(int connfd) {
    m_conn = async_file::async_warp(connfd);
    do_read();
  }

  void do_read() {
    fmt::print("Start reading...\n");
    // hard to manage the lifetime of captured this
    m_conn.async_read(m_buf, [this](size_t n) {
      if (n == 0) {
        fmt::print("Connection terminated due to EOF: {}\n", m_conn.m_fd);
        do_close();
        return;
      }
      // fmt::print("Read {} bytes: {}\n", n, std::string_view(m_buf.data(),
      // n));
      m_req_parser.push_chunk(m_buf.subspan(0, n));
      // fmt::print("request_finished: {}\n", m_req_parser.request_finished());
      // fmt::print("header_finished: {}\n", m_req_parser.header_finished());
      // fmt::print("body: {}\n", m_req_parser.body());
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
    fmt::print("Responding.\n");
    do_read(); // keep-alive
  }

  void do_close() {
    m_conn.close_file();
    delete this; // the other way of managing lifetime is shared_ptr
  }
};

struct http_connection_accepter {
  async_file m_listen;
  address_resolver::address m_addr;

  void do_init() {
    std::string port = "8080";
    fmt::print("Listening 127.0.0.1:{}\n", port);
    address_resolver resolver;
    resolver.resolve("127.0.0.1", port);
    auto entry = resolver.get_first_entry();
    m_listen = async_file::async_warp(entry.create_socket_and_bind());
    m_listen.async_accept(&m_addr.m_addr, &m_addr.m_addrlen, [](int connfd) {
      fmt::print("Connection accepted: {}\n", connfd);
      http_connection_handler *conn_handler = new http_connection_handler{};
    });
  }
};

void server() {

  epollfd = epoll_create1(0);
  // allocate conn_handler on heap, make it live out of this scope
  auto conn_handler = new http_connection_handler{};
  conn_handler->do_init(connid);

  struct epoll_event events[10];

  while (true) {
    int ret = epoll_wait(epollfd, events, 10, -1);
    if (ret < 0) {
      throw;
    }
    for (int i = 0; i < ret; i++) {
      auto cb = callback<>::from_address(events[i].data.ptr);
      cb();
    }
  }
  fmt::print("All tasks are done.\n");
}

int main() {
  try {
    server();
  } catch (const std::exception &e) {
    fmt::print("Error: {}\n", e.what());
  };
  return 0;
}
