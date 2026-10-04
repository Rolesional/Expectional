#pragma once
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>

// Minimal JSON utility for the WS protocol. Sadece ihtiyacımız olan kadar:
//  - String literal escape (writer)
//  - Object builder (writer)
//  - Field finder (parser): obje içinde "field":VALUE bulup tipine göre çeker.
//
// Sıkı mü hassasiyet hedeflemiyor; protokol mesajlarımız basit/flat.

namespace lic_json {

inline void escape_to(std::ostringstream& out, std::string_view s) {
  out << '"';
  for (char c : s) {
    switch (c) {
      case '"':  out << "\\\""; break;
      case '\\': out << "\\\\"; break;
      case '\n': out << "\\n";  break;
      case '\r': out << "\\r";  break;
      case '\t': out << "\\t";  break;
      case '\b': out << "\\b";  break;
      case '\f': out << "\\f";  break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
          out << buf;
        } else {
          out << c;
        }
    }
  }
  out << '"';
}

class Builder {
 public:
  Builder() { os_ << '{'; }
  Builder& add_str(std::string_view k, std::string_view v) {
    sep();
    escape_to(os_, k);
    os_ << ':';
    escape_to(os_, v);
    return *this;
  }
  Builder& add_int(std::string_view k, long long v) {
    sep();
    escape_to(os_, k);
    os_ << ':' << v;
    return *this;
  }
  Builder& add_uint64(std::string_view k, uint64_t v) {
    sep();
    escape_to(os_, k);
    os_ << ':' << v;
    return *this;
  }
  Builder& add_bool(std::string_view k, bool v) {
    sep();
    escape_to(os_, k);
    os_ << ':' << (v ? "true" : "false");
    return *this;
  }
  /// Raw JSON parça (örn iç-içe obje). Caller geçerli JSON sağlamalı.
  Builder& add_raw(std::string_view k, std::string_view raw_value) {
    sep();
    escape_to(os_, k);
    os_ << ':' << raw_value;
    return *this;
  }
  std::string done() {
    os_ << '}';
    auto s = os_.str();
    os_.str("");
    os_.clear();
    return s;
  }

 private:
  std::ostringstream os_;
  bool first_ = true;
  void sep() {
    if (!first_) os_ << ',';
    first_ = false;
  }
};

// ---- Parser helpers ---------------------------------------------------------

namespace detail {

inline size_t skip_ws(const std::string& s, size_t i) {
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) ++i;
  return i;
}

inline size_t read_string(const std::string& s, size_t i, std::string& out) {
  if (i >= s.size() || s[i] != '"') return std::string::npos;
  ++i;
  out.clear();
  while (i < s.size()) {
    char c = s[i];
    if (c == '"') return i + 1;
    if (c == '\\' && i + 1 < s.size()) {
      char n = s[i + 1];
      switch (n) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'u': {
          if (i + 5 >= s.size()) return std::string::npos;
          // BMP only, dümdüz UTF-8 kodlama (yeterli)
          unsigned int cp = 0;
          for (int k = 0; k < 4; ++k) {
            cp <<= 4;
            char h = s[i + 2 + k];
            if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
            else if (h >= 'a' && h <= 'f') cp |= (unsigned)(10 + h - 'a');
            else if (h >= 'A' && h <= 'F') cp |= (unsigned)(10 + h - 'A');
            else return std::string::npos;
          }
          if (cp < 0x80) {
            out += static_cast<char>(cp);
          } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
          } else {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
          }
          i += 4;
          break;
        }
        default: out += n; break;
      }
      i += 2;
    } else {
      out += c;
      ++i;
    }
  }
  return std::string::npos;
}

inline size_t skip_value(const std::string& s, size_t i) {
  i = skip_ws(s, i);
  if (i >= s.size()) return std::string::npos;
  char c = s[i];
  if (c == '"') {
    std::string tmp;
    return read_string(s, i, tmp);
  }
  if (c == '{' || c == '[') {
    char open = c;
    char close = (c == '{') ? '}' : ']';
    int depth = 1;
    ++i;
    while (i < s.size() && depth > 0) {
      char x = s[i];
      if (x == '"') {
        std::string tmp;
        i = read_string(s, i, tmp);
        if (i == std::string::npos) return i;
        continue;
      }
      if (x == open) ++depth;
      else if (x == close) --depth;
      ++i;
    }
    return depth == 0 ? i : std::string::npos;
  }
  // number / bool / null
  while (i < s.size() && s[i] != ',' && s[i] != '}' && s[i] != ']' &&
         s[i] != ' ' && s[i] != '\n' && s[i] != '\r' && s[i] != '\t') {
    ++i;
  }
  return i;
}

/// JSON obje string'i içinde "key" alanını arar; bulursa value-start
/// pozisyonunu döner, yoksa npos.
inline size_t find_key(const std::string& s, std::string_view key) {
  size_t i = 0;
  i = skip_ws(s, i);
  if (i >= s.size() || s[i] != '{') return std::string::npos;
  ++i;
  while (true) {
    i = skip_ws(s, i);
    if (i >= s.size()) return std::string::npos;
    if (s[i] == '}') return std::string::npos;
    std::string k;
    i = read_string(s, i, k);
    if (i == std::string::npos) return i;
    i = skip_ws(s, i);
    if (i >= s.size() || s[i] != ':') return std::string::npos;
    ++i;
    i = skip_ws(s, i);
    if (k == key) return i;
    size_t end = skip_value(s, i);
    if (end == std::string::npos) return end;
    i = skip_ws(s, end);
    if (i < s.size() && s[i] == ',') ++i;
    else if (i < s.size() && s[i] == '}') return std::string::npos;
  }
}

}  // namespace detail

inline bool get_string(const std::string& obj, std::string_view key, std::string& out) {
  size_t pos = detail::find_key(obj, key);
  if (pos == std::string::npos) return false;
  pos = detail::skip_ws(obj, pos);
  if (pos >= obj.size() || obj[pos] != '"') return false;
  return detail::read_string(obj, pos, out) != std::string::npos;
}

inline bool get_int(const std::string& obj, std::string_view key, long long& out) {
  size_t pos = detail::find_key(obj, key);
  if (pos == std::string::npos) return false;
  pos = detail::skip_ws(obj, pos);
  size_t end = detail::skip_value(obj, pos);
  if (end == std::string::npos) return false;
  try {
    out = std::stoll(obj.substr(pos, end - pos));
    return true;
  } catch (...) {
    return false;
  }
}

inline bool get_bool(const std::string& obj, std::string_view key, bool& out) {
  size_t pos = detail::find_key(obj, key);
  if (pos == std::string::npos) return false;
  pos = detail::skip_ws(obj, pos);
  if (obj.compare(pos, 4, "true") == 0) { out = true; return true; }
  if (obj.compare(pos, 5, "false") == 0) { out = false; return true; }
  return false;
}

/// Nested object: returns the substring including {...}.
inline bool get_object(const std::string& obj, std::string_view key, std::string& out) {
  size_t pos = detail::find_key(obj, key);
  if (pos == std::string::npos) return false;
  pos = detail::skip_ws(obj, pos);
  if (pos >= obj.size() || obj[pos] != '{') return false;
  size_t end = detail::skip_value(obj, pos);
  if (end == std::string::npos) return false;
  out = obj.substr(pos, end - pos);
  return true;
}

}  // namespace lic_json
