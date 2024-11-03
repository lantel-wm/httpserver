#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fmt/format.h>
#include <map>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <thread>
#include <unistd.h>
#include <vector>


#define CHECK_CALL(func, ...) check_error(#func, func(__VA_ARGS__))

int check_error(const char* msg, int res) {
    if (res == -1) {
        fmt::print("{}: {}\n", msg, strerror(errno));
        throw;
    }
    return res;
}

ssize_t check_error(const char* msg, ssize_t res) {
    if (res == -1) {
        fmt::print("{}: {}\n", msg, strerror(errno));
        throw;
    }
    return res;
}

struct socket_address_fatptr {
    struct sockaddr* m_addr;
    socklen_t m_addrlen;
};

struct socket_address_storage {
    union {
        struct sockaddr m_addr;
        struct sockaddr_storage m_addr_storage;
    };
    socklen_t m_addrlen = sizeof(sockaddr_storage);

    operator socket_address_fatptr() {
        return {&m_addr, m_addrlen};
    }
};

struct address_resolved_entry {
    struct addrinfo* m_curr = nullptr;

    socket_address_fatptr get_address() const {
        return {m_curr->ai_addr, m_curr->ai_addrlen};
    }

    int create_socket() const {
        int sockfd = CHECK_CALL(socket, m_curr->ai_family, m_curr->ai_socktype, m_curr->ai_protocol);
        return sockfd;
    }

    int create_socket_and_bind() const {
        int sockfd = create_socket();
        socket_address_fatptr serve_addr = get_address();
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

struct address_resolver {
    struct addrinfo* m_head = nullptr;

    void resolve(const std::string& name, const std::string& service) {
        int err = getaddrinfo(name.c_str(), service.c_str(), NULL, &m_head);
        if (err != 0) {
            fmt::print("getaddrinfo: {} {}", gai_strerror(err), err);
            throw;
        }
    }

    address_resolved_entry get_first_entry() {
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

using StringMap = std::map<std::string, std::string>;

struct http11_header_parser {
    std::string m_header;        // "GET / HTTP/1.1\r\nHost: 127.0.0.1:8080\r\nUser-Agent: curl/7.81.0\r\nAccept: */*"
    std::string m_heading_line;  // "GET / HTTP/1.1"
    StringMap m_header_keys;     // {"Host": "127.0.0.1:8080", "Accept": "*/*", ...}
    std::string m_body;          // The over-reading body (if exists)
    size_t content_length;
    bool m_header_finished;
    bool m_body_finished;

    void _parse_header() {
        size_t pos = m_header.find("\r\n");
        while (pos != std::string::npos) {
            pos += 2; // skip "\r\n"
            size_t next_pos = m_header.find("\r\n", pos);
            size_t line_len = std::string::npos;
            if (next_pos != std::string::npos) {
                line_len = next_pos - pos;
            }
            std::string line = m_header.substr(pos, line_len);
            size_t colon = line.find(": ");
            if (colon != std::string::npos) {
                std::string key = line.substr(0, colon);
                std::string value = line.substr(colon + 2);
                std::transform(key.begin(), key.end(), key.begin(), [] (char c) {
                    if ('A' <= c && c <= 'Z')
                        c += 'a' - 'A';
                    return c;
                });
                if (key == "content-length") {
                    content_length = std::stoi(value);
                }
            }
            pos = next_pos;
        }
    }

    void push_chunk(std::string_view chunk) {

    }

    [[nodiscard]] bool header_finished() const {
        return m_header_finished;
    }
};


template <typename HeaderParser = http11_header_parser>
struct http_request_parser {
    HeaderParser m_header_parser;
    std::string m_header;
    std::string m_body;
    size_t m_content_length;
    bool m_header_finished;
    bool m_body_finished;

    [[nodiscard]] bool need_more_chunks() const {
        return !m_body_finished;
    }

    void push_chunk(std::string_view chunk) {
        if (!m_header_parser.header_finished()) {
            m_header_parser.push_chunk(chunk);
            if (m_header_parser.header_finished()) {
                m_content_length
            }
            
        }
    }
};

std::vector<std::thread> pool;

int main() {
    fmt::print("Listening 127.0.0.1:8080\n");
    address_resolver resolver;
    resolver.resolve("127.0.0.1", "8080");
    auto entry = resolver.get_first_entry();
    int listenfd = entry.create_socket_and_bind();
    CHECK_CALL(listen, listenfd, SOMAXCONN);
    socket_address_storage addr;
    while (true) {
        int connid = CHECK_CALL(accept, listenfd, &addr.m_addr, &addr.m_addrlen);
        pool.emplace_back([connid] {
            char buf[1024];
            http_request_parser req_parse;
            do {
                size_t n = CHECK_CALL(read, connid, buf, sizeof(buf));
                req_parse.push_chunk(std::string_view(buf, n));
            } while (req_parse.need_more_chunks());
            std::string header = req_parse.m_header;
            std::string body = req_parse.m_body;
            fmt::print("Request header: {}\n", header);
            fmt::print("Request body: {}\n", body);
            std::string res = fmt::format("HTTP/1.1 200 OK\r\nServer: co_http\r\nConnection: close\r\nContent-length: {}\r\n\r\n{}", body.size(), body);
            fmt::print("Response: {}\n", res);
            CHECK_CALL(write, connid, res.data(), res.size());
            close(connid);
        });
    }
    for (auto& t: pool) {
        t.join();
    }
    return 0;
}