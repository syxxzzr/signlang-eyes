#include "piper_phonemizer.hpp"

#include <boost/json.hpp>
#include <cpp-pinyin/G2pglobal.h>
#include <cpp-pinyin/Pinyin.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace signlang::speech_tts {
  namespace {

    constexpr float kDefaultNoiseScale = 0.667F;
    constexpr float kDefaultLengthScale = 1.0F;
    constexpr float kDefaultNoiseW = 0.8F;

    [[nodiscard]] auto read_text_file(const std::string& path) -> std::string {
      auto input = std::ifstream{path};
      if (!input) {
        throw std::runtime_error("Failed to open Piper voice config: " + path);
      }
      return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    }

    [[nodiscard]] auto require_object(const boost::json::value& value, const std::string& context)
        -> const boost::json::object& {
      if (!value.is_object()) {
        throw std::runtime_error(context + " must be a JSON object");
      }
      return value.as_object();
    }

    [[nodiscard]] auto optional_float(const boost::json::object& object, const char* key, float default_value) -> float {
      const auto* value = object.if_contains(key);
      if (value == nullptr) {
        return default_value;
      }
      if (!value->is_number()) {
        throw std::runtime_error(std::string{"Piper config field must be a number: "} + key);
      }
      return static_cast<float>(value->to_number<double>());
    }

    [[nodiscard]] auto required_uint32(const boost::json::object& object, const char* key) -> std::uint32_t {
      const auto* value = object.if_contains(key);
      if (value == nullptr || !value->is_int64() || value->as_int64() < 0) {
        throw std::runtime_error(std::string{"Piper config field must be a non-negative integer: "} + key);
      }
      return static_cast<std::uint32_t>(value->as_int64());
    }

    [[nodiscard]] auto optional_uint32(const boost::json::object& object, const char* key, std::uint32_t default_value)
        -> std::uint32_t {
      const auto* value = object.if_contains(key);
      if (value == nullptr) {
        return default_value;
      }
      if (!value->is_int64() || value->as_int64() < 0) {
        throw std::runtime_error(std::string{"Piper config field must be a non-negative integer: "} + key);
      }
      return static_cast<std::uint32_t>(value->as_int64());
    }

    [[nodiscard]] auto load_phoneme_id_map(const boost::json::object& root)
        -> std::unordered_map<std::string, std::vector<std::int64_t>> {
      const auto* map_value = root.if_contains("phoneme_id_map");
      if (map_value == nullptr) {
        throw std::runtime_error("Piper config is missing phoneme_id_map");
      }

      const auto& map_object = require_object(*map_value, "Piper phoneme_id_map");
      auto id_map = std::unordered_map<std::string, std::vector<std::int64_t>>{};
      for (const auto& item : map_object) {
        if (!item.value().is_array()) {
          throw std::runtime_error("Piper phoneme_id_map values must be arrays");
        }

        auto ids = std::vector<std::int64_t>{};
        for (const auto& id_value : item.value().as_array()) {
          if (!id_value.is_int64()) {
            throw std::runtime_error("Piper phoneme id must be an integer");
          }
          ids.push_back(id_value.as_int64());
        }
        if (ids.empty()) {
          throw std::runtime_error("Piper phoneme_id_map contains an empty id list");
        }

        id_map.emplace(std::string{item.key()}, std::move(ids));
      }

      for (const auto* required_token : {"^", "$", "_"}) {
        if (id_map.find(required_token) == id_map.end()) {
          throw std::runtime_error(std::string{"Piper phoneme_id_map is missing required token: "} + required_token);
        }
      }

      return id_map;
    }

    void replace_all(std::string& value, const std::string& from, const std::string& to) {
      if (from.empty()) {
        return;
      }

      std::size_t position = 0;
      while ((position = value.find(from, position)) != std::string::npos) {
        value.replace(position, from.size(), to);
        position += to.size();
      }
    }

  } // namespace

  auto load_piper_voice_config(const std::string& config_path) -> PiperVoiceConfig {
    auto parsed = boost::json::parse(read_text_file(config_path));
    const auto& root = require_object(parsed, "Piper config");

    const auto* audio_value = root.if_contains("audio");
    if (audio_value == nullptr) {
      throw std::runtime_error("Piper config is missing audio");
    }
    const auto& audio = require_object(*audio_value, "Piper audio config");

    auto inference = boost::json::object{};
    if (const auto* inference_value = root.if_contains("inference"); inference_value != nullptr) {
      inference = require_object(*inference_value, "Piper inference config");
    }

    return PiperVoiceConfig{
        .sample_rate_hz = required_uint32(audio, "sample_rate"),
        .noise_scale = optional_float(inference, "noise_scale", kDefaultNoiseScale),
        .length_scale = optional_float(inference, "length_scale", kDefaultLengthScale),
        .noise_w = optional_float(inference, "noise_w", kDefaultNoiseW),
        .num_speakers = optional_uint32(root, "num_speakers", 1),
        .phoneme_id_map = load_phoneme_id_map(root),
    };
  }

  PiperPhonemizer::PiperPhonemizer(PiperVoiceConfig config, const std::string& dictionary_path) :
      config_{std::move(config)} {
    if (dictionary_path.empty()) {
      throw std::runtime_error("cpp-pinyin dictionary path must not be empty");
    }

    Pinyin::setDictionaryPath(std::filesystem::path{dictionary_path});
    const auto pinyin = Pinyin::Pinyin{};
    if (!pinyin.initialized()) {
      throw std::runtime_error("Failed to initialize cpp-pinyin with dictionary path: " + dictionary_path);
    }
  }

  auto PiperPhonemizer::phonemize_to_ids(const std::string& text) const -> std::vector<std::int64_t> {
    auto pinyin = Pinyin::Pinyin{};
    if (!pinyin.initialized()) {
      throw std::runtime_error("cpp-pinyin is not initialized");
    }

    auto ids = std::vector<std::int64_t>{};
    append_token_ids("^", ids);

    const auto result = pinyin.hanziToPinyin(text, Pinyin::ManTone::Style::TONE3, Pinyin::Error::Default, false, false,
                                             true);
    for (const auto& item : result) {
      if (!item.error && !item.pinyin.empty()) {
        append_pinyin_syllable(item.pinyin, ids);
        append_token_ids("_", ids);
        continue;
      }

      if (!item.hanzi.empty() && has_token(item.hanzi)) {
        append_token_ids(item.hanzi, ids);
        append_token_ids("_", ids);
      }
    }

    append_token_ids("$", ids);
    if (ids.size() <= 2) {
      throw std::runtime_error("cpp-pinyin produced no phoneme ids for text");
    }
    return ids;
  }

  auto PiperPhonemizer::config() const -> const PiperVoiceConfig& { return config_; }

  auto PiperPhonemizer::has_token(const std::string& token) const -> bool {
    return config_.phoneme_id_map.find(token) != config_.phoneme_id_map.end();
  }

  void PiperPhonemizer::append_token_ids(const std::string& token, std::vector<std::int64_t>& ids) const {
    const auto found = config_.phoneme_id_map.find(token);
    if (found == config_.phoneme_id_map.end()) {
      throw std::runtime_error("Piper phoneme_id_map does not contain token: " + token);
    }

    ids.insert(ids.end(), found->second.begin(), found->second.end());
  }

  void PiperPhonemizer::append_pinyin_syllable(const std::string& syllable, std::vector<std::int64_t>& ids) const {
    auto normalized = syllable;
    replace_all(normalized, "u:", "v");
    replace_all(normalized, "ü", "v");

    for (unsigned char character : normalized) {
      if (std::isdigit(character)) {
        continue;
      }

      auto token = std::string{1, static_cast<char>(std::tolower(character))};
      if (!has_token(token)) {
        throw std::runtime_error("Piper phoneme_id_map does not contain cpp-pinyin token: " + token);
      }
      append_token_ids(token, ids);
    }
  }

} // namespace signlang::speech_tts
