#ifndef HTTP_WRITER_HPP
#define HTTP_WRITER_HPP

#include "bytes_buffer.hpp"
#include <string>

struct http_response_writer {
  bytes_buffer m_buffer;

  void begin_header(int status) {
    // headline (response): "HTTP/1.1 200 OK"
    // TODO: change "OK" according to status code
    m_buffer.append("HTTP/1.1 " + std::to_string(status) + " OK\r\n");
  }

  void write_header(std::string key, std::string value) {
    m_buffer.append(key + ": " + value + "\r\n");
  }

  void end_header() { m_buffer.append("\r\n"); }

  bytes_buffer &buffer() { return m_buffer; }
};

#endif // HTTP_WRITER_HPP
