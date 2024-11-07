#ifndef HTTP_PARSER_HPP
#define HTTP_PARSER_HPP

#include <cassert>
#include <map>
#include <stdexcept>
#include <string>

using string_map = std::map<std::string, std::string>;

struct http11_header_parser {
  std::string
      m_header; // "GET / HTTP/1.1\r\nHost: 127.0.0.1:8080\r\nUser-Agent:
                // curl/7.81.0\r\nAccept: */*"
  std::string m_headline;   // "GET / HTTP/1.1"
  string_map m_header_keys; // {"Host": "127.0.0.1:8080", "Accept": "*/*", ...}
  std::string m_body;       // The over-reading body (if exists)
  size_t content_length;
  bool m_header_finished;
  bool m_body_finished;

  void _parse_header() {
    std::string_view header = m_header;
    size_t pos = header.find("\r\n", 0, 2);
    m_headline = std::string(header.substr(0, pos));
    while (pos != std::string::npos) {
      pos += 2; // skip "\r\n"
      size_t next_pos = m_header.find("\r\n", pos, 2);
      size_t line_len = std::string::npos;
      if (next_pos != std::string::npos) {
        line_len = next_pos - pos;
      }
      std::string_view line = header.substr(pos, line_len);
      size_t colon = line.find(": ", 0, 2);
      if (colon != std::string::npos) {
        std::string key = std::string(line.substr(0, colon));
        std::string_view value = line.substr(colon + 2);
        // transform to lower case
        for (char &c : key) {
          if ('A' <= c && c <= 'Z')
            c += 'a' - 'A';
        }
        m_header_keys.insert_or_assign(std::move(key), value);
      }
      pos = next_pos;
    }
  }

  void push_chunk(std::string_view chunk) {
    assert(!m_header_finished);
    size_t old_size = m_header.size();
    m_header.append(chunk);
    std::string_view header = m_header;
    if (old_size < 4) {
      old_size = 4;
    }
    old_size -= 4; // strip "\r\n\r\n"
    size_t header_len = header.find("\r\n\r\n", old_size, 4);
    if (header_len != std::string::npos) {
      m_header_finished = true;
      m_body = m_header.substr(header_len + 4);
      m_header.resize(header_len);
      _parse_header();
    }
  }

  [[nodiscard]] bool header_finished() const { return m_header_finished; }

  std::string &headline() { return m_headline; }

  std::string &headers_raw() { return m_header; }

  string_map &headers() { return m_header_keys; }

  std::string &extra_body() { return m_body; }
};

template <typename HeaderParser = http11_header_parser>
struct _http_parser_base {
  HeaderParser m_header_parser;
  std::string m_header;
  std::string m_body;
  size_t m_content_length;
  bool m_header_finished;
  bool m_body_finished;

  [[nodiscard]] bool body_finished() const {
    return m_header_parser.header_finished();
  }

  [[nodiscard]] bool request_finished() const { return m_body_finished; }

  std::string &headers_raw() { return m_header_parser.headers_raw(); }

  string_map &headers() { return m_header_parser.headers(); }

  std::string headline() { return m_header_parser.headline(); }

  std::string _headline_first() {
    // headline (request): "GET / HTTP/1.1"
    // headline (response): "HTTP/1.1 200 OK"
    std::string &line = headline();
    size_t space = line.find(" ");
    if (space == std::string::npos) {
      return "";
    }
    return line.substr(0, space);
  }

  std::string _headline_second() {
    // headline (request): "GET / HTTP/1.1"
    // headline (response): "HTTP/1.1 200 OK"
    std::string &headline = m_header_parser.headline();
    size_t space1 = headline.find(" ");
    if (space1 == std::string::npos) {
      return "";
    }
    size_t space2 = headline.find(" ", space1);
    if (space2 == std::string::npos) {
      return "";
    }
    return headline.substr(space1, space2);
  }

  std::string _headline_third() {
    // headline (request): "GET / HTTP/1.1"
    // headline (response): "HTTP/1.1 200 OK"
    std::string &headline = m_header_parser.headline();
    size_t space1 = headline.find(" ");
    if (space1 == std::string::npos) {
      return "";
    }
    size_t space2 = headline.find(" ", space1);
    if (space2 == std::string::npos) {
      return "";
    }
    return headline.substr(space2);
  }

  std::string &body() { return m_header_parser.extra_body(); }

  size_t _extract_content_length() {
    string_map &headers = m_header_parser.headers();
    auto it = headers.find("content-length");
    if (it == headers.end()) { // not found
      return 0;
    }
    try {
      return std::stoi(it->second);
    } catch (std::invalid_argument &e) {
      return 0;
    }
  }

  void push_chunk(std::string_view chunk) {
    if (!m_header_parser.header_finished()) {
      m_header_parser.push_chunk(chunk);
      if (m_header_parser.header_finished()) {
        m_content_length = _extract_content_length();
      }
    }
  }
};

template <typename HeaderParser = http11_header_parser>
struct http_request_parser : public _http_parser_base<HeaderParser> {
  std::string method() { return this->_headline_first(); }

  std::string url() { return this->_headline_second(); }

  std::string http_version() { return this->_headline_third(); }
};

template <typename HeaderParser = http11_header_parser>
struct http_response_parser : public _http_parser_base<HeaderParser> {
  std::string http_version() { return this->_headline_first(); }

  int status() {
    std::string s = this->_headline_second();
    try {
      return std::stoi(s);
    } catch (
        std::logic_error &e) { // std::out_of_range and std::invalid_argument
      return -1;
    }
  }

  std::string status_string() { return this->_headline_second(); }
};

#endif // HTTP_PARSER_HPP
