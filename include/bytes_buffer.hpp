#ifndef BYTES_BUFFER_HPP
#define BYTES_BUFFER_HPP

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <vector>

struct bytes_const_view {
  const char *m_data;
  size_t m_size;

  const char *data() const noexcept { return m_data; }

  size_t size() const noexcept { return m_size; }

  const char *begin() const noexcept { return data(); }

  const char *end() const noexcept { return data() + size(); }

  bytes_const_view subspan(size_t start,
                           size_t len = static_cast<size_t>(-1)) const {
    if (start > size()) {
      throw std::out_of_range("bytes_const_view::subspan");
    }
    if (len > size() - start) {
      len = size() - start;
    }
    return {data() + start, len};
  }

  operator std::string_view() const noexcept {
    return std::string_view{data(), size()};
  }
};

struct bytes_view {
  char *m_data;
  size_t m_size;

  char *data() const noexcept { return m_data; }

  size_t size() const noexcept { return m_size; }

  char *begin() const noexcept { return data(); }

  char *end() const noexcept { return data() + size(); }

  bytes_view subspan(size_t start, size_t len) const {
    if (start > size()) {
      throw std::out_of_range("bytes_view::subspan");
    }
    if (len > size() - start) {
      len = size() - start;
    }
    return {data() + start, len};
  }

  operator bytes_const_view() const noexcept {
    return bytes_const_view{data(), size()};
  }

  operator std::string_view() const noexcept {
    return std::string_view{data(), size()};
  }
};

struct bytes_buffer {
  std::vector<char> m_data;

  bytes_buffer() = default;
  bytes_buffer(bytes_buffer &&) = default;
  bytes_buffer &operator=(bytes_buffer &&) = default;
  explicit bytes_buffer(const bytes_buffer &) =
      default; // forbids implicit copy

  explicit bytes_buffer(size_t n) : m_data(n) {}

  const char *data() const noexcept { return m_data.data(); }

  char *data() noexcept { return m_data.data(); }

  size_t size() const noexcept { return m_data.size(); }

  const char *begin() const noexcept { return data(); }

  char *begin() noexcept { return data(); }

  const char *end() const noexcept { return data() + size(); }

  char *end() noexcept { return data() + size(); }

  bytes_const_view subspan(size_t start, size_t len) const {
    return operator bytes_const_view().subspan(start, len);
  }

  bytes_view subspan(size_t start, size_t len) {
    return operator bytes_view().subspan(start, len);
  }

  operator bytes_const_view() const noexcept {
    return bytes_const_view{m_data.data(), m_data.size()};
  }

  operator bytes_view() noexcept {
    return bytes_view{m_data.data(), m_data.size()};
  }

  operator std::string_view() const noexcept {
    return std::string_view{m_data.data(), m_data.size()};
  }

  void append(bytes_const_view chunk) {
    m_data.insert(m_data.end(), chunk.begin(), chunk.end());
  }

  void append(std::string_view chunk) {
    m_data.insert(m_data.end(), chunk.begin(), chunk.end());
  }

  template <size_t N> void append_literal(const char (&literal)[N]) {
    // append C-style string literal
    append(std::string_view{literal, N - 1}); // N - 1 to strip '\0'
  }

  void clear() { m_data.clear(); }

  void resize(size_t n) { m_data.resize(n); }

  void reserve(size_t n) { m_data.reserve(n); }
};

template <size_t N> struct static_bytes_buffer {
  std::array<char, N> m_data;

  const char *data() const noexcept { return m_data.data(); }

  char *data() noexcept { return m_data.data(); }

  static constexpr size_t size() noexcept { return N; }

  operator bytes_const_view() const noexcept {
    return bytes_const_view{m_data.data(), N};
  }

  operator bytes_view() noexcept { return bytes_view{m_data.data(), N}; }

  operator std::string_view() const noexcept {
    return std::string_view{m_data.data(), m_data.size()};
  }
};

#endif // BYTES_BUFFER_HPP
