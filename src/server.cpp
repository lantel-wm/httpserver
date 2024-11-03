#include <cstddef>
#include <cstdio>
#include <fmt/format.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

struct address_resolver {
    struct addrinfo* m_head = nullptr;

    void resolve(const std::string& name, const std::string& service) {
        int err = getaddrinfo(name.c_str(), service.c_str(), NULL, &m_head);
        if (err != 0) {
            
        }
    }
};

int main() {
    struct addrinfo *addrinfo;
    int err = getaddrinfo("127.0.0.1", "80", NULL, &addrinfo);
    if (err != 0) {
        fmt::print("getaddrinfo: {}\n", gai_strerror(err));
        return 1;
    }

    for (struct addrinfo* current = addrinfo; current != NULL; current = current->ai_next) {
        fmt::print("family  : {}\n", addrinfo->ai_family);
        fmt::print("socktype: {}\n", addrinfo->ai_socktype);
        fmt::print("protocol: {}\n", addrinfo->ai_protocol);
    }

    int sockfd = socket(-1, addrinfo->ai_socktype, addrinfo->ai_protocol);
    if (sockfd == -1) {
        fmt::print("socket: {}\n", strerror(errno));
        return 1;
    }
}