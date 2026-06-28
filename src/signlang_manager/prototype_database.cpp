#include "prototype_database.hpp"

#include "SQLiteCpp.h"
#include "spdlog/spdlog.h"

#include <array>
#include <charconv>
#include <ctime>
#include <filesystem>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace signlang::signlang_manager {
  namespace {

    constexpr auto kSchemaVersion = int{1};
    constexpr const char* kFloat32Dtype = "f32";

    auto read_meta_int(SQLite::Database& database, const char* key) -> int {
      auto query = SQLite::Statement{database, "SELECT value FROM meta WHERE key = ?"};
      query.bind(1, key);
      if (!query.executeStep()) {
        throw std::runtime_error("missing prototype DB meta key");
      }

      const auto value = query.getColumn(0).getString();
      auto parsed = int{0};
      const auto* begin = value.data();
      const auto* end = begin + value.size();
      const auto [parse_end, parse_error] = std::from_chars(begin, end, parsed);
      if (parse_error != std::errc{} || parse_end != end) {
        throw std::runtime_error("prototype DB meta value must be an integer");
      }
      return parsed;
    }

    auto find_gesture_id(SQLite::Database& database, const std::string& gesture_name) -> std::optional<std::uint32_t> {
      auto query = SQLite::Statement{database, "SELECT id FROM gestures WHERE name = ?"};
      query.bind(1, gesture_name);
      if (!query.executeStep()) {
        return std::nullopt;
      }
      return query.getColumn(0).getUInt();
    }

    auto encoded_sample_to_blob(const EncodedSequence& encoded_sample, std::uint32_t embedding_dim)
        -> std::vector<float> {
      if (encoded_sample.empty()) {
        throw std::runtime_error("Encoded gesture sample must contain at least one frame");
      }

      auto blob = std::vector<float>{};
      blob.reserve(encoded_sample.size() * embedding_dim);
      for (const auto& frame : encoded_sample) {
        if (frame.size() != embedding_dim) {
          throw std::runtime_error("Encoded gesture sample embedding dimension mismatch");
        }
        blob.insert(blob.end(), frame.begin(), frame.end());
      }
      return blob;
    }

    auto blob_to_encoded_sample(const float* blob, int blob_bytes, std::uint32_t frame_count,
                                std::uint32_t embedding_dim) -> EncodedSequence {
      if (frame_count == 0 || embedding_dim == 0) {
        throw std::runtime_error("Prototype sample has invalid dimensions");
      }

      const auto expected_bytes = static_cast<std::size_t>(frame_count) * embedding_dim * sizeof(float);
      if (blob == nullptr || blob_bytes < 0 || static_cast<std::size_t>(blob_bytes) != expected_bytes) {
        throw std::runtime_error("Prototype sample blob size mismatch");
      }

      auto sample = EncodedSequence(frame_count);
      for (std::uint32_t frame_index = 0; frame_index < frame_count; ++frame_index) {
        const auto* frame_begin = blob + (static_cast<std::size_t>(frame_index) * embedding_dim);
        sample[frame_index].assign(frame_begin, frame_begin + embedding_dim);
      }
      return sample;
    }

    auto utc_backup_timestamp() -> std::string {
      const auto now = std::time(nullptr);
      auto utc_time = std::tm{};
      gmtime_r(&now, &utc_time);

      auto buffer = std::array<char, 32>{};
      if (std::strftime(buffer.data(), buffer.size(), "%Y%m%dT%H%M%SZ", &utc_time) == 0) {
        return "unknown-time";
      }
      return buffer.data();
    }

    auto backup_path_for(const std::filesystem::path& database_path) -> std::filesystem::path {
      namespace fs = std::filesystem;

      const auto base_path = fs::path{database_path.string() + ".invalid-" + utc_backup_timestamp() + ".bak"};
      auto candidate = base_path;
      auto error = std::error_code{};
      for (auto suffix = 1U; fs::exists(candidate, error) && !error; ++suffix) {
        candidate = fs::path{base_path.string() + "." + std::to_string(suffix)};
      }

      return candidate;
    }

  } // namespace

  PrototypeDatabase::PrototypeDatabase(std::string path, std::uint32_t embedding_dim) :
      path_{std::move(path)}, embedding_dim_{embedding_dim} {
    if (path_.empty()) {
      throw std::runtime_error("Prototype database path must not be empty");
    }
    if (embedding_dim_ == 0) {
      throw std::runtime_error("Prototype database embedding dimension must be greater than zero");
    }
  }

  auto PrototypeDatabase::path() const -> const std::string& { return path_; }

  auto PrototypeDatabase::embedding_dim() const -> std::uint32_t { return embedding_dim_; }

  void PrototypeDatabase::ensure_valid_empty_or_existing() {
    if (!is_valid_schema()) {
      recreate_empty_schema();
    }
  }

  auto PrototypeDatabase::is_valid_schema() const -> bool {
    try {
      if (!std::filesystem::exists(path_)) {
        return false;
      }

      auto database = SQLite::Database{path_, SQLite::OPEN_READONLY};
      if (!database.tableExists("meta") || !database.tableExists("gestures") || !database.tableExists("samples")) {
        return false;
      }

      const auto schema_version = read_meta_int(database, "schema_version");
      const auto db_embedding_dim = read_meta_int(database, "embedding_dim");
      return schema_version == kSchemaVersion && db_embedding_dim == static_cast<int>(embedding_dim_);
    } catch (const std::exception&) {
      return false;
    }
  }

  void PrototypeDatabase::recreate_empty_schema() const {
    namespace fs = std::filesystem;

    const auto db_path = fs::path{path_};
    if (const auto parent = db_path.parent_path(); !parent.empty()) {
      fs::create_directories(parent);
    }

    std::error_code error;
    if (fs::exists(db_path, error) && !error) {
      const auto backup_path = backup_path_for(db_path);
      fs::copy_file(db_path, backup_path, fs::copy_options::none, error);
      if (error) {
        throw std::runtime_error("Failed to backup invalid prototype database '" + db_path.string() + "' to '" +
                                 backup_path.string() + "': " + error.message());
      }
      spdlog::warn("Prototype database schema is invalid; backed up '{}' to '{}' before recreating it",
                   db_path.string(), backup_path.string());
    }

    error.clear();
    fs::remove(db_path, error);
    if (error) {
      throw std::runtime_error("Failed to remove invalid prototype database '" + db_path.string() +
                               "': " + error.message());
    }

    auto database = SQLite::Database{path_, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE};
    auto transaction = SQLite::Transaction{database};

    database.exec("CREATE TABLE meta ("
                  "  key TEXT PRIMARY KEY,"
                  "  value TEXT NOT NULL"
                  ");"
                  "CREATE TABLE gestures ("
                  "  id INTEGER PRIMARY KEY,"
                  "  name TEXT NOT NULL,"
                  "  enabled INTEGER NOT NULL DEFAULT 1"
                  ");"
                  "CREATE TABLE samples ("
                  "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                  "  gesture_id INTEGER NOT NULL,"
                  "  frame_count INTEGER NOT NULL,"
                  "  embedding_dim INTEGER NOT NULL,"
                  "  dtype TEXT NOT NULL DEFAULT 'f32',"
                  "  data BLOB NOT NULL,"
                  "  weight REAL NOT NULL DEFAULT 1.0,"
                  "  FOREIGN KEY (gesture_id) REFERENCES gestures(id)"
                  ");"
                  "CREATE INDEX idx_samples_gesture_id ON samples(gesture_id);");

    auto meta_insert = SQLite::Statement{database, "INSERT INTO meta(key, value) VALUES (?, ?)"};
    meta_insert.bind(1, "schema_version");
    meta_insert.bind(2, std::to_string(kSchemaVersion));
    meta_insert.exec();

    meta_insert.reset();
    meta_insert.clearBindings();
    meta_insert.bind(1, "embedding_dim");
    meta_insert.bind(2, std::to_string(embedding_dim_));
    meta_insert.exec();

    transaction.commit();
  }

  auto PrototypeDatabase::list_gestures() const -> std::vector<GestureInfo> {
    auto database = SQLite::Database{path_, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE};
    auto query = SQLite::Statement{database,
                                   "SELECT g.id, g.name, g.enabled, COUNT(s.id) "
                                   "FROM gestures g "
                                   "LEFT JOIN samples s ON s.gesture_id = g.id "
                                   "GROUP BY g.id, g.name, g.enabled "
                                   "ORDER BY g.id"};

    auto gestures = std::vector<GestureInfo>{};
    while (query.executeStep()) {
      gestures.push_back(GestureInfo{
          .id = query.getColumn(0).getUInt(),
          .name = query.getColumn(1).getString(),
          .enabled = query.getColumn(2).getInt() != 0,
          .sample_count = query.getColumn(3).getUInt(),
      });
    }

    return gestures;
  }

  auto PrototypeDatabase::load_gesture_samples(const std::string& gesture_name) const -> std::vector<EncodedSequence> {
    if (gesture_name.empty()) {
      throw std::runtime_error("Gesture name must not be empty");
    }

    auto database = SQLite::Database{path_, SQLite::OPEN_READONLY};
    const auto gesture_id = find_gesture_id(database, gesture_name);
    if (!gesture_id.has_value()) {
      return {};
    }

    auto query = SQLite::Statement{database,
                                   "SELECT frame_count, embedding_dim, dtype, data "
                                   "FROM samples "
                                   "WHERE gesture_id = ? "
                                   "ORDER BY id"};
    query.bind(1, gesture_id.value());

    auto samples = std::vector<EncodedSequence>{};
    while (query.executeStep()) {
      const auto frame_count = static_cast<std::uint32_t>(query.getColumn(0).getUInt());
      const auto embedding_dim = static_cast<std::uint32_t>(query.getColumn(1).getUInt());
      const auto dtype = query.getColumn(2).getString();
      const auto* blob = static_cast<const float*>(query.getColumn(3).getBlob());
      const auto blob_bytes = query.getColumn(3).getBytes();

      if (embedding_dim != embedding_dim_) {
        throw std::runtime_error("Prototype sample embedding dimension mismatch");
      }
      if (dtype != kFloat32Dtype) {
        throw std::runtime_error("Unsupported prototype sample dtype: " + dtype);
      }

      samples.push_back(blob_to_encoded_sample(blob, blob_bytes, frame_count, embedding_dim));
    }

    return samples;
  }

  auto PrototypeDatabase::add_gesture_sample(const std::string& gesture_name, const EncodedSequence& encoded_sample,
                                             bool replace_existing) -> std::uint32_t {
    if (gesture_name.empty()) {
      throw std::runtime_error("Gesture name must not be empty");
    }

    auto blob = encoded_sample_to_blob(encoded_sample, embedding_dim_);
    if (blob.size() > static_cast<std::size_t>(std::numeric_limits<int>::max() / sizeof(float))) {
      throw std::runtime_error("Encoded gesture sample is too large to store in SQLite");
    }

    auto database = SQLite::Database{path_, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE};
    auto transaction = SQLite::Transaction{database, SQLite::TransactionBehavior::IMMEDIATE};

    auto gesture_id = find_gesture_id(database, gesture_name);
    if (!gesture_id.has_value()) {
      auto insert_gesture = SQLite::Statement{database, "INSERT INTO gestures(name, enabled) VALUES (?, 1)"};
      insert_gesture.bind(1, gesture_name);
      insert_gesture.exec();
      gesture_id = static_cast<std::uint32_t>(database.getLastInsertRowid());
    } else if (replace_existing) {
      auto delete_samples = SQLite::Statement{database, "DELETE FROM samples WHERE gesture_id = ?"};
      delete_samples.bind(1, gesture_id.value());
      delete_samples.exec();

      auto enable_gesture = SQLite::Statement{database, "UPDATE gestures SET enabled = 1 WHERE id = ?"};
      enable_gesture.bind(1, gesture_id.value());
      enable_gesture.exec();
    }

    auto insert_sample =
        SQLite::Statement{database,
                          "INSERT INTO samples(gesture_id, frame_count, embedding_dim, dtype, data, weight) "
                          "VALUES (?, ?, ?, ?, ?, 1.0)"};
    insert_sample.bind(1, gesture_id.value());
    insert_sample.bind(2, static_cast<std::uint32_t>(encoded_sample.size()));
    insert_sample.bind(3, embedding_dim_);
    insert_sample.bind(4, kFloat32Dtype);
    insert_sample.bind(5, blob.data(), static_cast<int>(blob.size() * sizeof(float)));
    insert_sample.exec();

    transaction.commit();
    return gesture_id.value();
  }

  auto PrototypeDatabase::replace_gesture_samples(const std::string& gesture_name,
                                                  const std::vector<EncodedSequence>& samples) -> std::uint32_t {
    if (gesture_name.empty()) {
      throw std::runtime_error("Gesture name must not be empty");
    }
    if (samples.empty()) {
      throw std::runtime_error("Gesture must contain at least one prototype sample");
    }

    auto blobs = std::vector<std::vector<float>>{};
    blobs.reserve(samples.size());
    for (const auto& sample : samples) {
      auto blob = encoded_sample_to_blob(sample, embedding_dim_);
      if (blob.size() > static_cast<std::size_t>(std::numeric_limits<int>::max() / sizeof(float))) {
        throw std::runtime_error("Encoded gesture sample is too large to store in SQLite");
      }
      blobs.push_back(std::move(blob));
    }

    auto database = SQLite::Database{path_, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE};
    auto transaction = SQLite::Transaction{database, SQLite::TransactionBehavior::IMMEDIATE};

    auto gesture_id = find_gesture_id(database, gesture_name);
    if (!gesture_id.has_value()) {
      auto insert_gesture = SQLite::Statement{database, "INSERT INTO gestures(name, enabled) VALUES (?, 1)"};
      insert_gesture.bind(1, gesture_name);
      insert_gesture.exec();
      gesture_id = static_cast<std::uint32_t>(database.getLastInsertRowid());
    } else {
      auto enable_gesture = SQLite::Statement{database, "UPDATE gestures SET enabled = 1 WHERE id = ?"};
      enable_gesture.bind(1, gesture_id.value());
      enable_gesture.exec();
    }

    auto delete_samples = SQLite::Statement{database, "DELETE FROM samples WHERE gesture_id = ?"};
    delete_samples.bind(1, gesture_id.value());
    delete_samples.exec();

    auto insert_sample =
        SQLite::Statement{database,
                          "INSERT INTO samples(gesture_id, frame_count, embedding_dim, dtype, data, weight) "
                          "VALUES (?, ?, ?, ?, ?, 1.0)"};
    for (std::size_t i = 0; i < samples.size(); ++i) {
      insert_sample.bind(1, gesture_id.value());
      insert_sample.bind(2, static_cast<std::uint32_t>(samples[i].size()));
      insert_sample.bind(3, embedding_dim_);
      insert_sample.bind(4, kFloat32Dtype);
      insert_sample.bind(5, blobs[i].data(), static_cast<int>(blobs[i].size() * sizeof(float)));
      insert_sample.exec();
      insert_sample.reset();
      insert_sample.clearBindings();
    }

    transaction.commit();
    return gesture_id.value();
  }

  auto PrototypeDatabase::delete_gesture(std::uint32_t gesture_id) -> bool {
    auto database = SQLite::Database{path_, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE};
    auto transaction = SQLite::Transaction{database, SQLite::TransactionBehavior::IMMEDIATE};

    auto exists = SQLite::Statement{database, "SELECT id FROM gestures WHERE id = ?"};
    exists.bind(1, gesture_id);
    if (!exists.executeStep()) {
      return false;
    }

    auto delete_samples = SQLite::Statement{database, "DELETE FROM samples WHERE gesture_id = ?"};
    delete_samples.bind(1, gesture_id);
    delete_samples.exec();

    auto delete_gesture = SQLite::Statement{database, "DELETE FROM gestures WHERE id = ?"};
    delete_gesture.bind(1, gesture_id);
    delete_gesture.exec();

    transaction.commit();
    return true;
  }

  auto PrototypeDatabase::delete_gesture(const std::string& gesture_name) -> bool {
    auto database = SQLite::Database{path_, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE};
    auto gesture_id = find_gesture_id(database, gesture_name);
    if (!gesture_id.has_value()) {
      return false;
    }
    return delete_gesture(gesture_id.value());
  }

} // namespace signlang::signlang_manager
