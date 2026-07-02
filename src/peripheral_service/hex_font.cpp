#include "hex_font.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace signlang::peripheral_service {
  namespace {

    [[nodiscard]] auto hex_nibble(char value) -> std::uint8_t {
      if (value >= '0' && value <= '9') {
        return static_cast<std::uint8_t>(value - '0');
      }
      if (value >= 'A' && value <= 'F') {
        return static_cast<std::uint8_t>(value - 'A' + 10);
      }
      if (value >= 'a' && value <= 'f') {
        return static_cast<std::uint8_t>(value - 'a' + 10);
      }
      throw std::runtime_error("invalid hex digit in font file");
    }

    [[nodiscard]] auto parse_hex_u32(const std::string& value) -> std::uint32_t {
      auto result = std::uint32_t{0};
      for (const auto ch : value) {
        result = static_cast<std::uint32_t>((result << 4U) | hex_nibble(ch));
      }
      return result;
    }

    [[nodiscard]] auto parse_hex_bytes(const std::string& value) -> std::vector<std::uint8_t> {
      if ((value.size() % 2U) != 0U) {
        throw std::runtime_error("font glyph hex payload has odd length");
      }
      auto bytes = std::vector<std::uint8_t>{};
      bytes.reserve(value.size() / 2U);
      for (auto index = std::size_t{0}; index < value.size(); index += 2U) {
        bytes.push_back(static_cast<std::uint8_t>((hex_nibble(value[index]) << 4U) | hex_nibble(value[index + 1U])));
      }
      return bytes;
    }

  } // namespace

  HexFont::HexFont(const std::string& path) { load(path); }

  auto HexFont::find(std::uint32_t codepoint) const -> const HexGlyph* {
    const auto iter = glyphs_.find(codepoint);
    if (iter == glyphs_.end()) {
      return nullptr;
    }
    return &iter->second;
  }

  auto HexFont::fallback_glyph() const -> HexGlyph {
    if (const auto* glyph = find(0x25A1U); glyph != nullptr) {
      return *glyph;
    }
    if (const auto* glyph = find(static_cast<std::uint32_t>('?')); glyph != nullptr) {
      return *glyph;
    }

    auto rows = std::vector<std::uint16_t>(16, 0);
    rows.front() = 0xFFFFU;
    rows.back() = 0xFFFFU;
    for (auto row = std::size_t{1}; row + 1U < rows.size(); ++row) {
      rows[row] = 0x8001U;
    }
    return HexGlyph{0x25A1U, 16, 16, std::move(rows)};
  }

  void HexFont::load(const std::string& path) {
    std::ifstream input{path};
    if (!input) {
      throw std::runtime_error("failed to open hex font file: " + path);
    }

    auto line = std::string{};
    while (std::getline(input, line)) {
      if (!line.empty()) {
        parse_line(line);
      }
    }
    if (glyphs_.empty()) {
      throw std::runtime_error("hex font file has no glyphs: " + path);
    }
  }

  void HexFont::parse_line(const std::string& line) {
    const auto separator = line.find(':');
    if (separator == std::string::npos || separator == 0U || separator + 1U >= line.size()) {
      throw std::runtime_error("invalid hex font line");
    }

    const auto codepoint = parse_hex_u32(line.substr(0, separator));
    const auto bytes = parse_hex_bytes(line.substr(separator + 1U));
    if (bytes.size() != 16U && bytes.size() != 32U) {
      return;
    }

    const auto glyph_width = static_cast<std::uint8_t>(bytes.size() == 16U ? 8U : 16U);
    auto rows = std::vector<std::uint16_t>{};
    rows.reserve(16);
    for (auto row = std::size_t{0}; row < 16U; ++row) {
      if (glyph_width == 8U) {
        rows.push_back(static_cast<std::uint16_t>(bytes[row]) << 8U);
      } else {
        rows.push_back(static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[row * 2U]) << 8U) |
                                                  bytes[row * 2U + 1U]));
      }
    }

    glyphs_[codepoint] = HexGlyph{codepoint, glyph_width, 16, std::move(rows)};
  }

  auto decode_utf8_codepoints(const std::string& text) -> std::vector<std::uint32_t> {
    auto codepoints = std::vector<std::uint32_t>{};
    auto index = std::size_t{0};
    while (index < text.size()) {
      const auto byte = static_cast<unsigned char>(text[index]);
      if (byte < 0x80U) {
        codepoints.push_back(byte);
        ++index;
        continue;
      }

      auto extra_bytes = std::size_t{0};
      auto codepoint = std::uint32_t{0};
      if ((byte & 0xE0U) == 0xC0U) {
        extra_bytes = 1;
        codepoint = byte & 0x1FU;
      } else if ((byte & 0xF0U) == 0xE0U) {
        extra_bytes = 2;
        codepoint = byte & 0x0FU;
      } else if ((byte & 0xF8U) == 0xF0U) {
        extra_bytes = 3;
        codepoint = byte & 0x07U;
      } else {
        codepoints.push_back(static_cast<std::uint32_t>('?'));
        ++index;
        continue;
      }

      if (index + extra_bytes >= text.size()) {
        codepoints.push_back(static_cast<std::uint32_t>('?'));
        break;
      }

      auto valid = true;
      for (auto offset = std::size_t{1}; offset <= extra_bytes; ++offset) {
        const auto continuation = static_cast<unsigned char>(text[index + offset]);
        if ((continuation & 0xC0U) != 0x80U) {
          valid = false;
          break;
        }
        codepoint = static_cast<std::uint32_t>((codepoint << 6U) | (continuation & 0x3FU));
      }

      codepoints.push_back(valid ? codepoint : static_cast<std::uint32_t>('?'));
      index += valid ? extra_bytes + 1U : 1U;
    }
    return codepoints;
  }

} // namespace signlang::peripheral_service
