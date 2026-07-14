#include "prototype_database.hpp"

#include "SQLiteCpp/SQLiteCpp.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace signlang::signlang_det {
  namespace {

    constexpr auto kSchemaVersion = int{2};
    constexpr const char* kPreprocessing = "hand168-temporal";

    auto read_meta(SQLite::Database& database, const char* key) -> std::string {
      auto statement = SQLite::Statement{database, "SELECT value FROM meta WHERE key = ?"};
      statement.bind(1, key);
      if (!statement.executeStep()) {
        throw std::runtime_error(std::string{"prototype database is missing metadata: "} + key);
      }
      return statement.getColumn(0).getString();
    }

    auto read_meta_int(SQLite::Database& database, const char* key) -> int {
      const auto value = read_meta(database, key);
      auto parsed = int{};
      const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
      if (error != std::errc{} || end != value.data() + value.size()) {
        throw std::runtime_error(std::string{"prototype database metadata is not an integer: "} + key);
      }
      return parsed;
    }

    auto read_meta_float(SQLite::Database& database, const char* key) -> float {
      const auto value = read_meta(database, key);
      std::size_t consumed = 0;
      const auto parsed = std::stof(value, &consumed);
      if (consumed != value.size() || !std::isfinite(parsed)) {
        throw std::runtime_error(std::string{"prototype database metadata is not a finite number: "} + key);
      }
      return parsed;
    }

    auto has_columns(SQLite::Database& database, const char* table,
                     std::initializer_list<const char*> required) -> bool {
      auto available = std::unordered_set<std::string>{};
      auto query = SQLite::Statement{database, std::string{"PRAGMA table_info("} + table + ")"};
      while (query.executeStep()) {
        available.emplace(query.getColumn(1).getString());
      }
      return std::all_of(required.begin(), required.end(),
                         [&](const auto* name) { return available.find(name) != available.end(); });
    }

    auto find_gesture_id(SQLite::Database& database, const std::string& name) -> std::optional<std::uint32_t> {
      auto statement = SQLite::Statement{database, "SELECT id FROM gestures WHERE name = ?"};
      statement.bind(1, name);
      return statement.executeStep() ? std::optional<std::uint32_t>{statement.getColumn(0).getUInt()} : std::nullopt;
    }

    auto timestamp() -> std::string {
      const auto now = std::time(nullptr);
      auto utc = std::tm{};
      gmtime_r(&now, &utc);
      auto buffer = std::array<char, 32>{};
      return std::strftime(buffer.data(), buffer.size(), "%Y%m%dT%H%M%SZ", &utc) == 0 ? "unknown" : buffer.data();
    }

    auto finite(const FrameEmbedding& embedding) -> bool {
      for (const auto value : embedding) {
        if (!std::isfinite(value)) {
          return false;
        }
      }
      return true;
    }

    auto frame_blob(const GesturePrototype& sample) -> std::vector<float> {
      if (sample.frames.empty() || sample.frames.size() != sample.valid_length || !finite(sample.pooled)) {
        throw std::runtime_error("invalid prototype sample");
      }
      auto blob = std::vector<float>{};
      blob.reserve(sample.frames.size() * kEmbeddingDim);
      for (const auto& frame : sample.frames) {
        if (!finite(frame)) {
          throw std::runtime_error("prototype sample contains non-finite values");
        }
        blob.insert(blob.end(), frame.begin(), frame.end());
      }
      return blob;
    }

    auto read_sample(SQLite::Statement& statement) -> GesturePrototype {
      const auto id = statement.getColumn(0).getUInt();
      const auto valid_length = statement.getColumn(1).getUInt();
      const auto* frames = static_cast<const float*>(statement.getColumn(2).getBlob());
      const auto frame_bytes = statement.getColumn(2).getBytes();
      const auto* pooled = static_cast<const float*>(statement.getColumn(3).getBlob());
      const auto pooled_bytes = statement.getColumn(3).getBytes();
      if (valid_length == 0 || valid_length > kMaxSequenceLength || frames == nullptr || pooled == nullptr ||
          frame_bytes != static_cast<int>(valid_length * kEmbeddingDim * sizeof(float)) ||
          pooled_bytes != static_cast<int>(kEmbeddingDim * sizeof(float))) {
        throw std::runtime_error("prototype sample blob dimensions are invalid");
      }
      auto sample = GesturePrototype{id, std::vector<FrameEmbedding>(valid_length), {}, valid_length,
                                     static_cast<float>(statement.getColumn(4).getDouble()),
                                     static_cast<std::uint64_t>(statement.getColumn(5).getInt64())};
      for (std::uint32_t frame = 0; frame < valid_length; ++frame) {
        std::copy_n(frames + static_cast<std::size_t>(frame) * kEmbeddingDim, kEmbeddingDim,
                    sample.frames[frame].begin());
        if (!finite(sample.frames[frame])) {
          throw std::runtime_error("prototype frame contains non-finite values");
        }
      }
      std::copy_n(pooled, kEmbeddingDim, sample.pooled.begin());
      if (!finite(sample.pooled) || !std::isfinite(sample.quality)) {
        throw std::runtime_error("prototype metadata contains non-finite values");
      }
      return sample;
    }

  } // namespace

  PrototypeDatabase::PrototypeDatabase(std::string path) : path_{std::move(path)} {
    if (path_.empty()) throw std::invalid_argument("prototype database path must not be empty");
  }

  auto PrototypeDatabase::path() const -> const std::string& { return path_; }

  void PrototypeDatabase::backup_incompatible_schema() const {
    namespace fs = std::filesystem;
    auto backup = fs::path{path_ + "." + timestamp() + ".backup"};
    auto suffix = 1U;
    while (fs::exists(backup)) {
      backup = fs::path{path_ + "." + timestamp() + ".backup." + std::to_string(suffix++)};
    }
    fs::rename(path_, backup);
    spdlog::warn("Backed up incompatible prototype database '{}' to '{}'", path_, backup.string());
  }

  void PrototypeDatabase::create_empty_schema() const {
    namespace fs = std::filesystem;
    const auto parent = fs::path{path_}.parent_path();
    if (!parent.empty()) {
      fs::create_directories(parent);
    }
    auto database = SQLite::Database{path_, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE};
    database.exec("PRAGMA foreign_keys = ON;");
    auto transaction = SQLite::Transaction{database};
    database.exec("CREATE TABLE meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);"
                  "CREATE TABLE gestures("
                  " id INTEGER PRIMARY KEY, name TEXT NOT NULL UNIQUE, enabled INTEGER NOT NULL DEFAULT 1,"
                  " dtw_threshold REAL NOT NULL, coarse_threshold REAL NOT NULL, calibration_status INTEGER NOT NULL,"
                  " sample_count INTEGER NOT NULL, created_at INTEGER NOT NULL, updated_at INTEGER NOT NULL);"
                  "CREATE TABLE samples("
                  " id INTEGER PRIMARY KEY AUTOINCREMENT, gesture_id INTEGER NOT NULL REFERENCES gestures(id) ON DELETE CASCADE,"
                  " valid_length INTEGER NOT NULL, frame_embeddings BLOB NOT NULL, pooled_embedding BLOB NOT NULL,"
                  " quality REAL NOT NULL, captured_at INTEGER NOT NULL);"
                  "CREATE INDEX idx_samples_gesture_id ON samples(gesture_id);");
    auto insert = SQLite::Statement{database, "INSERT INTO meta(key, value) VALUES (?, ?)"};
    const auto values = std::array<std::pair<const char*, std::string>, 5>{{
        {"schema_version", std::to_string(kSchemaVersion)},
        {"preprocessing", kPreprocessing}, {"max_sequence_length", std::to_string(kMaxSequenceLength)},
        {"embedding_dim", std::to_string(kEmbeddingDim)}, {"padding_value", std::to_string(kPaddingValue)}}};
    for (const auto& [key, value] : values) {
      insert.bind(1, key);
      insert.bind(2, value);
      insert.exec();
      insert.reset();
      insert.clearBindings();
    }
    transaction.commit();
  }

  void PrototypeDatabase::ensure_valid_empty_or_existing() {
    namespace fs = std::filesystem;
    if (!fs::exists(path_)) {
      create_empty_schema();
      return;
    }
    auto incompatible = false;
    try {
      auto database = SQLite::Database{path_, SQLite::OPEN_READONLY};
      incompatible = !database.tableExists("meta") || !database.tableExists("gestures") ||
                     !database.tableExists("samples") || read_meta_int(database, "schema_version") != kSchemaVersion ||
                     !has_columns(database, "gestures", {"id", "name", "enabled", "dtw_threshold",
                         "coarse_threshold", "calibration_status", "sample_count", "created_at", "updated_at"}) ||
                     !has_columns(database, "samples", {"id", "gesture_id", "valid_length", "frame_embeddings",
                         "pooled_embedding", "quality", "captured_at"});
    } catch (const std::exception&) {
      incompatible = true;
    }
    if (incompatible) {
      backup_incompatible_schema();
      create_empty_schema();
      return;
    }

    {
      auto database = SQLite::Database{path_, SQLite::OPEN_READONLY};
      if (read_meta(database, "preprocessing") != kPreprocessing ||
          read_meta_int(database, "max_sequence_length") != static_cast<int>(kMaxSequenceLength) ||
          read_meta_int(database, "embedding_dim") != static_cast<int>(kEmbeddingDim) ||
          read_meta_float(database, "padding_value") != kPaddingValue) {
        throw std::runtime_error("prototype database does not match the temporal encoder; record gestures again");
      }
    }
  }

  auto PrototypeDatabase::load_store() const -> PrototypeStore {
    auto database = SQLite::Database{path_, SQLite::OPEN_READONLY};
    auto gestures = std::vector<GesturePrototypeSet>{};
    auto query = SQLite::Statement{database,
        "SELECT id, name, dtw_threshold, coarse_threshold, calibration_status FROM gestures WHERE enabled != 0 ORDER BY id"};
    while (query.executeStep()) {
      const auto dtw_threshold = static_cast<float>(query.getColumn(2).getDouble());
      const auto coarse_threshold = static_cast<float>(query.getColumn(3).getDouble());
      const auto calibration_value = query.getColumn(4).getUInt();
      if (!std::isfinite(dtw_threshold) || dtw_threshold < 0.0F || !std::isfinite(coarse_threshold) ||
          coarse_threshold < 0.0F || calibration_value > static_cast<std::uint32_t>(CalibrationStatus::Calibrated)) {
        throw std::runtime_error("gesture thresholds or calibration status are invalid");
      }
      auto gesture = GesturePrototypeSet{query.getColumn(0).getUInt(), query.getColumn(1).getString(),
          dtw_threshold, coarse_threshold, static_cast<CalibrationStatus>(calibration_value), {}};
      auto samples = SQLite::Statement{database,
          "SELECT id, valid_length, frame_embeddings, pooled_embedding, quality, captured_at FROM samples WHERE gesture_id = ? ORDER BY id"};
      samples.bind(1, gesture.gesture_id);
      while (samples.executeStep()) {
        gesture.samples.push_back(read_sample(samples));
      }
      if (!gesture.samples.empty()) {
        gestures.push_back(std::move(gesture));
      }
    }
    auto store = PrototypeStore{};
    store.replace(std::move(gestures));
    return store;
  }

  auto PrototypeDatabase::list_gestures() const -> std::vector<GestureInfo> {
    auto database = SQLite::Database{path_, SQLite::OPEN_READONLY};
    auto query = SQLite::Statement{database,
        "SELECT id, name, enabled, sample_count, calibration_status FROM gestures ORDER BY id"};
    auto result = std::vector<GestureInfo>{};
    while (query.executeStep()) {
      result.push_back(GestureInfo{query.getColumn(0).getUInt(), query.getColumn(1).getString(),
          query.getColumn(2).getInt() != 0, query.getColumn(3).getUInt(),
          static_cast<CalibrationStatus>(query.getColumn(4).getUInt())});
    }
    return result;
  }

  auto PrototypeDatabase::load_gesture_samples(const std::string& name) const -> std::vector<GesturePrototype> {
    auto database = SQLite::Database{path_, SQLite::OPEN_READONLY};
    const auto id = find_gesture_id(database, name);
    if (!id) {
      return {};
    }
    auto query = SQLite::Statement{database,
        "SELECT id, valid_length, frame_embeddings, pooled_embedding, quality, captured_at FROM samples WHERE gesture_id = ? ORDER BY id"};
    query.bind(1, *id);
    auto result = std::vector<GesturePrototype>{};
    while (query.executeStep()) {
      result.push_back(read_sample(query));
    }
    return result;
  }

  auto PrototypeDatabase::replace_gesture_samples(const std::string& name,
      const std::vector<GesturePrototype>& samples, float dtw_threshold, float coarse_threshold,
      CalibrationStatus calibration) -> std::uint32_t {
    if (name.empty() || samples.empty()) {
      throw std::invalid_argument("gesture name and samples must not be empty");
    }
    if (!std::isfinite(dtw_threshold) || dtw_threshold < 0.0F || !std::isfinite(coarse_threshold) ||
        coarse_threshold < 0.0F || static_cast<std::uint32_t>(calibration) >
            static_cast<std::uint32_t>(CalibrationStatus::Calibrated)) {
      throw std::invalid_argument("gesture thresholds or calibration status are invalid");
    }
    auto blobs = std::vector<std::vector<float>>{};
    for (const auto& sample : samples) {
      blobs.push_back(frame_blob(sample));
    }
    auto database = SQLite::Database{path_, SQLite::OPEN_READWRITE};
    database.exec("PRAGMA foreign_keys = ON;");
    auto transaction = SQLite::Transaction{database, SQLite::TransactionBehavior::IMMEDIATE};
    auto id = find_gesture_id(database, name);
    const auto now = static_cast<std::int64_t>(std::time(nullptr));
    if (!id) {
      auto insert = SQLite::Statement{database,
          "INSERT INTO gestures(name, enabled, dtw_threshold, coarse_threshold, calibration_status, sample_count, created_at, updated_at) VALUES (?, 1, ?, ?, ?, ?, ?, ?)"};
      insert.bind(1, name); insert.bind(2, dtw_threshold); insert.bind(3, coarse_threshold);
      insert.bind(4, static_cast<std::uint32_t>(calibration)); insert.bind(5, static_cast<std::uint32_t>(samples.size()));
      insert.bind(6, now); insert.bind(7, now); insert.exec();
      id = static_cast<std::uint32_t>(database.getLastInsertRowid());
    } else {
      auto update = SQLite::Statement{database,
          "UPDATE gestures SET enabled=1, dtw_threshold=?, coarse_threshold=?, calibration_status=?, sample_count=?, updated_at=? WHERE id=?"};
      update.bind(1, dtw_threshold); update.bind(2, coarse_threshold);
      update.bind(3, static_cast<std::uint32_t>(calibration)); update.bind(4, static_cast<std::uint32_t>(samples.size()));
      update.bind(5, now); update.bind(6, *id); update.exec();
      auto remove = SQLite::Statement{database, "DELETE FROM samples WHERE gesture_id=?"};
      remove.bind(1, *id); remove.exec();
    }
    auto insert_sample = SQLite::Statement{database,
        "INSERT INTO samples(gesture_id, valid_length, frame_embeddings, pooled_embedding, quality, captured_at) VALUES (?, ?, ?, ?, ?, ?)"};
    for (std::size_t index = 0; index < samples.size(); ++index) {
      insert_sample.bind(1, *id); insert_sample.bind(2, samples[index].valid_length);
      insert_sample.bind(3, blobs[index].data(), static_cast<int>(blobs[index].size() * sizeof(float)));
      insert_sample.bind(4, samples[index].pooled.data(), static_cast<int>(samples[index].pooled.size() * sizeof(float)));
      insert_sample.bind(5, samples[index].quality); insert_sample.bind(6, static_cast<std::int64_t>(samples[index].captured_at));
      insert_sample.exec(); insert_sample.reset(); insert_sample.clearBindings();
    }
    transaction.commit();
    return *id;
  }

  auto PrototypeDatabase::delete_gesture(std::uint32_t id) -> bool {
    auto database = SQLite::Database{path_, SQLite::OPEN_READWRITE};
    database.exec("PRAGMA foreign_keys = ON;");
    auto statement = SQLite::Statement{database, "DELETE FROM gestures WHERE id=?"};
    statement.bind(1, id);
    return statement.exec() != 0;
  }

  auto PrototypeDatabase::delete_gesture(const std::string& name) -> bool {
    auto database = SQLite::Database{path_, SQLite::OPEN_READONLY};
    const auto id = find_gesture_id(database, name);
    return id ? delete_gesture(*id) : false;
  }

} // namespace signlang::signlang_det
