#include "text_renderer.hpp"

#include <algorithm>

namespace signlang::peripheral_service {

  TextRenderer::TextRenderer(const HexFont& font, TextRenderOptions options) : font_{font}, options_{options} {
    options_.font_size = std::max<std::uint8_t>(1, options_.font_size);
  }

  auto TextRenderer::measure_width(const std::string& text) const -> std::uint16_t {
    const auto codepoints = decode_utf8_codepoints(text);
    auto width = std::uint16_t{0};
    auto first = true;
    for (const auto codepoint : codepoints) {
      const auto* glyph = font_.find(codepoint);
      const auto fallback = glyph == nullptr ? font_.fallback_glyph() : HexGlyph{};
      const auto& selected = glyph == nullptr ? fallback : *glyph;
      if (!first) {
        width = static_cast<std::uint16_t>(width + options_.char_spacing);
      }
      width = static_cast<std::uint16_t>(width + scaled_width(selected));
      first = false;
    }
    return width;
  }

  void TextRenderer::draw_text(OledFramebuffer& framebuffer, const std::string& text, std::int16_t x,
                               std::uint8_t y) const {
    const auto codepoints = decode_utf8_codepoints(text);
    auto cursor = x;
    auto first = true;
    for (const auto codepoint : codepoints) {
      const auto* glyph = font_.find(codepoint);
      const auto fallback = glyph == nullptr ? font_.fallback_glyph() : HexGlyph{};
      const auto& selected = glyph == nullptr ? fallback : *glyph;
      if (!first) {
        cursor = static_cast<std::int16_t>(cursor + options_.char_spacing);
      }
      draw_glyph(framebuffer, selected, cursor, y);
      cursor = static_cast<std::int16_t>(cursor + scaled_width(selected));
      first = false;
    }
  }

  auto TextRenderer::scaled_width(const HexGlyph& glyph) const -> std::uint8_t {
    return static_cast<std::uint8_t>(std::max<std::uint16_t>(
        1U, static_cast<std::uint16_t>(glyph.width) * options_.font_size / std::max<std::uint8_t>(1, glyph.height)));
  }

  void TextRenderer::draw_glyph(OledFramebuffer& framebuffer, const HexGlyph& glyph, std::int16_t x,
                                std::uint8_t y) const {
    const auto output_width = scaled_width(glyph);
    const auto output_height = options_.font_size;
    for (auto dst_y = std::uint8_t{0}; dst_y < output_height; ++dst_y) {
      const auto src_y = static_cast<std::uint8_t>(static_cast<std::uint16_t>(dst_y) * glyph.height / output_height);
      for (auto dst_x = std::uint8_t{0}; dst_x < output_width; ++dst_x) {
        const auto src_x = static_cast<std::uint8_t>(static_cast<std::uint16_t>(dst_x) * glyph.width / output_width);
        const auto mask = static_cast<std::uint16_t>(0x8000U >> src_x);
        const auto set = (glyph.rows[src_y] & mask) != 0U;
        const auto target_x = static_cast<std::int16_t>(x + dst_x);
        const auto target_y = static_cast<std::int16_t>(y + dst_y);
        if (target_x >= 0 && target_y >= 0 && target_x < framebuffer.width() && target_y < framebuffer.height()) {
          framebuffer.set(static_cast<std::uint8_t>(target_x), static_cast<std::uint8_t>(target_y), set);
        }
      }
    }
  }

} // namespace signlang::peripheral_service
