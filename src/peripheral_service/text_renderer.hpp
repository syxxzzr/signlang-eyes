#ifndef SIGNLANG_EYES_PERIPHERAL_SERVICE_TEXT_RENDERER_HPP
#define SIGNLANG_EYES_PERIPHERAL_SERVICE_TEXT_RENDERER_HPP

#include "hex_font.hpp"
#include "oled_framebuffer.hpp"

#include <cstdint>
#include <string>

namespace signlang::peripheral_service {

  struct TextRenderOptions {
    std::uint8_t font_size = 16;
    std::uint8_t char_spacing = 1;
  };

  class TextRenderer {
  public:
    TextRenderer(const HexFont& font, TextRenderOptions options);

    [[nodiscard]] auto measure_width(const std::string& text) const -> std::uint16_t;
    void draw_text(OledFramebuffer& framebuffer, const std::string& text, std::int16_t x, std::uint8_t y) const;

  private:
    [[nodiscard]] auto scaled_width(const HexGlyph& glyph) const -> std::uint8_t;
    void draw_glyph(OledFramebuffer& framebuffer, const HexGlyph& glyph, std::int16_t x, std::uint8_t y) const;

    const HexFont& font_;
    TextRenderOptions options_;
  };

} // namespace signlang::peripheral_service

#endif // SIGNLANG_EYES_PERIPHERAL_SERVICE_TEXT_RENDERER_HPP
