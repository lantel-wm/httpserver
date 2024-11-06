#include <cassert>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fmt/format.h>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <vector>

const std::error_category& gai_category() {
    struct gai_category final : public std::error_category  {
        const char* name() const noexcept override {
            return "getaddrinfo";
        }
        std::string message(int err) const override {
            return gai_strerror(err);
        }
    };
    // singleton
    static gai_category instance;
    return instance;
} 

template <int Except = 0, typename T>
T check_error(const char* msg, T res) {
    if (res == -1) {
        if constexpr (Except != 0) {
            if (errno == Except) {
                return -1;
            }
        }
        fmt::print(stderr, "{}: {}\n", msg, strerror(errno));
        auto ec = std::error_code(errno, std::system_category());
        throw std::system_error(ec, msg);
    }
    return res;
}

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#define SOURCE_INFO_IMPL(file, line) "In " file ":" line ": "
// #define SOURCE_INFO() SOURCE_INFO_IMPL(__FILE__, __LINE__)
#define SOURCE_INFO() SOURCE_INFO_IMPL(__FILE__, TOSTRING(__LINE__))
#define CHECK_CALL(func, ...) check_error(SOURCE_INFO() #func, func(__VA_ARGS__))
#define CHECK_CALL_EXCEPT(Except, func, ...) check_error<Except>(SOURCE_INFO() #func, func(__VA_ARGS__))

ssize_t check_error(const char* msg, ssize_t res) {
    if (res == -1) {
        fmt::print("{}: {}\n", msg, strerror(errno));
        throw;
    }
    return res;
}


struct address_resolver {
    struct address_ref {
        struct sockaddr* m_addr;
        socklen_t m_addrlen;
    };

    struct address {
        union {
            struct sockaddr m_addr;
            struct sockaddr_storage m_addr_storage;
        };

        socklen_t m_addrlen = sizeof(struct sockaddr_storage);

        operator address_ref() {
            return {&m_addr, m_addrlen};
        }
    };

    struct address_info {
        struct addrinfo* m_curr = nullptr;

        address_ref get_address() const {
            return {m_curr->ai_addr, m_curr->ai_addrlen};
        }

        int create_socket() const {
            int sockfd = CHECK_CALL(socket, m_curr->ai_family, m_curr->ai_socktype, m_curr->ai_protocol);
            return sockfd;
        }

        int create_socket_and_bind() const {
            int sockfd = create_socket();
            address_ref serve_addr = get_address();
            CHECK_CALL(bind, sockfd, serve_addr.m_addr, serve_addr.m_addrlen);
            return sockfd;
        }

        [[nodiscard]] bool next_entry() {
            m_curr = m_curr->ai_next;
            if (m_curr == nullptr) {
                return false;
            }
            return true;
        }

    };

    struct addrinfo* m_head = nullptr;

    address_info resolve(const std::string& name, const std::string& service) {
        int err = getaddrinfo(name.c_str(), service.c_str(), NULL, &m_head);
        if (err != 0) {
            auto ec = std::error_code(err, gai_category());
            throw std::system_error(ec, name + ":" + service);
        }
        return {m_head};
    }

    address_info get_first_entry() {
        return {m_head};
    }

    address_resolver() = default;

    address_resolver(address_resolver&& that) : m_head(that.m_head) {
        that.m_head = nullptr;
    }

    ~address_resolver() {
        if (m_head) {
            freeaddrinfo(m_head);
        }
    }
};

using string_map = std::map<std::string, std::string>;

struct http11_header_parser {
    std::string m_header;        // "GET / HTTP/1.1\r\nHost: 127.0.0.1:8080\r\nUser-Agent: curl/7.81.0\r\nAccept: */*"
    std::string m_headline;  // "GET / HTTP/1.1"
    string_map m_header_keys;     // {"Host": "127.0.0.1:8080", "Accept": "*/*", ...}
    std::string m_body;          // The over-reading body (if exists)
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
                for (char& c: key) {
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

    [[nodiscard]] bool header_finished() const {
        return m_header_finished;
    }

    std::string& headline() {
        return m_headline;
    }

    std::string& headers_raw() {
        return m_header;
    }

    string_map& headers() {
        return m_header_keys;
    }

    std::string& extra_body() {
        return m_body;
    }
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

    [[nodiscard]] bool request_finished() const {
        return m_body_finished;
    }

    std::string& headers_raw() {
        return m_header_parser.headers_raw();
    }

    string_map& headers() {
        return m_header_parser.headers();
    }

    std::string headline() {
        return m_header_parser.headline();
    }

    std::string _headline_first() {
        // headline (request): "GET / HTTP/1.1"
        // headline (response): "HTTP/1.1 200 OK"
        std::string& line = headline();
        size_t space = line.find(" ");
        if (space == std::string::npos) {
            return "";
        }
        return line.substr(0, space);
    }

    std::string _headline_second() {
        // headline (request): "GET / HTTP/1.1"
        // headline (response): "HTTP/1.1 200 OK"
        std::string& headline = m_header_parser.headline();
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
        std::string& headline = m_header_parser.headline();
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

    std::string& body() {
        return m_header_parser.extra_body();
    }

    size_t _extract_content_length() {
        string_map& headers = m_header_parser.headers();
        auto it = headers.find("content-length");
        if (it == headers.end()) { // not found
            return 0;
        }
        try {
            return std::stoi(it->second);
        } catch (std::invalid_argument& e) {
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
struct http_request_parser: public _http_parser_base<HeaderParser> {
    std::string method() {
        return this->_headline_first();
    }

    std::string url() {
        return this->_headline_second();
    }

    std::string http_version() {
        return this->_headline_third();
    }
};

template <typename HeaderParser = http11_header_parser>
struct http_response_parser: public _http_parser_base<HeaderParser> {
    std::string http_version() {
        return this->_headline_first();
    }

    int status() {
        std::string s = this->_headline_second();
        try {
            return std::stoi(s);
        } catch (std::logic_error& e) { // std::out_of_range and std::invalid_argument
            return -1;
        }
    }

    std::string status_string() {
        return this->_headline_second();
    }
};

struct http_response_writer {
    std::string m_buffer;

    void begin_header(int status) {
        // headline (response): "HTTP/1.1 200 OK"
        // TODO: change "OK" according to status code
        m_buffer = "HTTP/1.1 " + std::to_string(status) + " OK\r\n";
    }

    void write_header(std::string key, std::string value) {
        m_buffer.append(key + ": " + value + "\r\n");
    }

    void end_header() {
        m_buffer.append("\r\n");
    }

    std::string& buffer() {
        return m_buffer;
    }

};

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
                char in_buffer[1024];
                _http_parser_base req_parse;
                do {
                    size_t n = CHECK_CALL(read, connid, in_buffer, sizeof(in_buffer));
                    // if EOF is read, close the connection
                    if (n == 0) {
                        close(connid);
                        fmt::print("Connection is terminated by client: {}\n", connid);
                        return;
                    }
                    req_parse.push_chunk(std::string_view(in_buffer, n));
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
                    break;
                if (CHECK_CALL_EXCEPT(EPIPE, write, connid, body.data(), body.size()) == -1)
                    break;

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