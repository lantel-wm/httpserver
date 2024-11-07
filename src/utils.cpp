#include "utils.hpp"

const std::error_category &gai_category() {
  struct gai_category final : public std::error_category {
    const char *name() const noexcept override { return "getaddrinfo"; }
    std::string message(int err) const override { return gai_strerror(err); }
  };
  // singleton
  static gai_category instance;
  return instance;
}

ssize_t check_error(const char *msg, ssize_t res) {
  if (res == -1) {
    fmt::print("{}: {}\n", msg, strerror(errno));
    throw;
  }
  return res;
}
