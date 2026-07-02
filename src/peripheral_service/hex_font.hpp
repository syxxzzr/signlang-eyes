#ifndef SIGNLANG_EYES_PERIPHERAL_SERVICE_HEX_FONT_HPP
#define SIGNLANG_EYES_PERIPHERAL_SERVICE_HEX_FONT_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace signlang::peripheral_service {

  struct HexGlyph {
    std::uint32_t codepoint;
    std::uint8_t width;
    std::uint8_t height;
    std::vector<std::uint16_t> rows;
  };

  class HexFont {
  public:
    explicit HexFont(const std::string& path);

    [[nodiscard]] auto find(std::uint32_t codepoint) const -> const HexGlyph*;
    [[nodiscard]] auto fallback_glyph() const -> HexGlyph;

  private:
    void load(const std::string& path);
    void parse_line(const std::string& line);

    std::unordered_map<std::uint32_t, HexGlyph> glyphs_;
  };

  [[nodiscard]] auto decode_utf8_codepoints(const std::string& text) -> std::vector<std::uint32_t>;

} // namespace signlang::peripheral_service

#endif // SIGNLANG_EYES_PERIPHERAL_SERVICE_HEX_FONT_HPP
