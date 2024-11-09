#ifndef UTILS_HPP
#define UTILS_HPP

#include <cstring>
#include <fmt/format.h>
#include <netdb.h>
#include <system_error>

const std::error_category &gai_category();

template <int Except = 0, typename T> T check_error(const char *msg, T res) {
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

// template <int Except1 = 0, int Except2 = 0, typename T>
// T check_error(const char *msg, T res) {
//   if (res == -1) {
//     if constexpr (Except1 != 0 || Except2 != 0) {
//       if (errno == Except1 || errno == Except2) {
//         return -1;
//       }
//     }
//     fmt::print(stderr, "{}: {}\n", msg, strerror(errno));
//     auto ec = std::error_code(errno, std::system_category());
//     throw std::system_error(ec, msg);
//   }
//   return res;
// }

ssize_t check_error(const char *msg, ssize_t res);

#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)

#define SOURCE_INFO_IMPL(file, line) "In " file ":" line ": "
#define SOURCE_INFO() SOURCE_INFO_IMPL(__FILE__, TOSTRING(__LINE__))
#define CHECK_CALL(func, ...)                                                  \
  check_error(SOURCE_INFO() #func, func(__VA_ARGS__))
#define CHECK_CALL_EXCEPT(Except, func, ...)                                   \
  check_error<Except>(SOURCE_INFO() #func, func(__VA_ARGS__))
// #define CHECK_CALL_EXCEPT2(Except1, Except2, func, ...)                        \
//   check_error<Except1, Except2>(SOURCE_INFO() #func, func(__VA_ARGS__))

#endif // UTILS_HPP
