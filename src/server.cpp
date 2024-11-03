#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <fmt/format.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define CHECK_CALL(func, ...) check_error(#func, func(__VA_ARGS__))

int check_error(const char* msg, int res) {
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

int main() {
    address_resolver resolver;
    resolver.resolve("127.0.0.1", "8080");
    auto entry = resolver.get_first_entry();
    int listenfd = entry.create_socket_and_bind();
    CHECK_CALL(listen, listenfd, SOMAXCONN);
    socket_address_storage addr;
    int connid = CHECK_CALL(accept, listenfd, &addr.m_addr, &addr.m_addrlen);
    
}