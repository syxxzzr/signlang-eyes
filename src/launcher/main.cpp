#include "program_options.hpp"

#include "common/logging.hpp"
#include "spdlog/spdlog.h"
#include "toml.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <csignal>
#include <ctime>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <sys/wait.h>
#include <unistd.h>
#include <variant>
#include <vector>

namespace signlang::launcher::ipc {

constexpr auto kAudioService    = "audio_capture";
constexpr auto kVideoService    = "video_capture";
constexpr auto kSpeechAsrOutput = "speech_asr_result";
constexpr auto kHandposeOutput  = "handpose_result";
constexpr auto kSignlangOutput  = "signlang_result";
constexpr auto kStateEvent      = "app_state_event";
constexpr auto kStateBlackboard = "app_state_blackboard";
constexpr auto kStateControl    = "app_state_control";
constexpr auto kAudioLocalizationBlackboard = "audio_source_localization";

} // namespace signlang::launcher::ipc

namespace {

constexpr auto kExeStateMachine  = "bin/state_machine";
constexpr auto kExeAudioFrontend = "bin/audio_frontend";
constexpr auto kExeVideoFrontend = "bin/video_frontend";
constexpr auto kExeSpeechAsr     = "bin/speech_asr";
constexpr auto kExeEnvSoundDet   = "bin/env_sound_det";
constexpr auto kExeHandposeDet   = "bin/handpose_det";
constexpr auto kExeSignlangDet   = "bin/signlang_det";

constexpr std::array kIpcKeys = {
  "input_service",
  "input-service",
  "output_service",
  "output-service",
  "state_event_service",
  "state-event-service",
  "state_blackboard_service",
  "state-blackboard-service",
  "state_control_service",
  "state-control-service",
  "localization_blackboard",
  "localization-blackboard",
};

void warn_ipc_keys_in_table(const toml::table& tbl, const std::string& section_name) {
  for (const auto& [key, node] : tbl) {
    for (const auto* ipc_key : kIpcKeys) {
      if (key == ipc_key) {
        spdlog::warn("[launcher] [{}] '{}' is ignored; IPC service names are hardcoded in the launcher",
                     section_name, key.str());
        break;
      }
    }
  }
}

void warn_ipc_keys_in_config(const toml::table& config) {
  constexpr std::array kSections = {
    "state_machine",
    "audio_frontend",
    "video_frontend",
    "speech_asr",
    "env_sound_det",
    "handpose_det",
    "signlang_det",
  };

  for (const auto* section_name : kSections) {
    if (const auto* section = config[section_name].as_table()) {
      warn_ipc_keys_in_table(*section, section_name);
    }
  }
}

struct ChildInfo {
  pid_t pid;
  std::string name;
};

struct LoggingConfig {
  std::uint64_t rotate_size = signlang::logging::kDefaultRotateSize;
  std::uint64_t retain_files = signlang::logging::kDefaultRetainFiles;
};

std::vector<ChildInfo> g_children;
volatile std::sig_atomic_t g_shutdown = 0;

void handle_signal(int /* sig */) { g_shutdown = 1; }

void terminate_all_children() {
  for (const auto& child : g_children) {
    spdlog::info("[launcher] terminating {} (pid {})", child.name, child.pid);
    kill(child.pid, SIGTERM);
  }
  for (const auto& child : g_children) {
    int status = 0;
    waitpid(child.pid, &status, 0);
  }
}

auto launch_child(const std::vector<std::string>& args) -> pid_t {
  int pipefd[2];
  if (pipe2(pipefd, O_CLOEXEC) < 0) {
    spdlog::error("[launcher] pipe2 failed: {}", std::strerror(errno));
    return -1;
  }

  const auto pid = fork();
  if (pid < 0) {
    spdlog::error("[launcher] fork failed: {}", std::strerror(errno));
    close(pipefd[0]);
    close(pipefd[1]);
    return -1;
  }

  if (pid == 0) {
    close(pipefd[0]);

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& arg : args) {
      argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    execvp(argv[0], argv.data());

    auto exec_err = errno;
    static_cast<void>(write(pipefd[1], &exec_err, sizeof(exec_err)));
    _exit(EXIT_FAILURE);
  }

  close(pipefd[1]);
  int child_err = 0;
  const auto n = read(pipefd[0], &child_err, sizeof(child_err));
  close(pipefd[0]);

  if (n > 0) {
    spdlog::error("[launcher] exec failed for {}: {}", args[0], std::strerror(child_err));
    int status = 0;
    waitpid(pid, &status, 0);
    return -1;
  }

  return pid;
}

auto opt_string(const toml::table& tbl, const char* key) -> std::optional<std::string> {
  if (auto node = tbl[key].as_string()) {
    return std::string{node->get()};
  }
  return std::nullopt;
}

auto opt_int(const toml::table& tbl, const char* key) -> std::optional<std::int64_t> {
  if (auto node = tbl[key].as_integer()) {
    return node->get();
  }
  return std::nullopt;
}

auto opt_double(const toml::table& tbl, const char* key) -> std::optional<double> {
  if (auto node = tbl[key].as_floating_point()) {
    return node->get();
  }
  return std::nullopt;
}

auto opt_bool(const toml::table& tbl, const char* key) -> std::optional<bool> {
  if (auto node = tbl[key].as_boolean()) {
    return node->get();
  }
  return std::nullopt;
}

void add_opt_str(std::vector<std::string>& args, const char* flag, const std::optional<std::string>& val) {
  if (val && !val->empty()) {
    args.emplace_back(flag);
    args.push_back(*val);
  }
}

void add_opt_int(std::vector<std::string>& args, const char* flag, const std::optional<std::int64_t>& val) {
  if (val) {
    args.emplace_back(flag);
    args.push_back(std::to_string(*val));
  }
}

void add_opt_double(std::vector<std::string>& args, const char* flag, const std::optional<double>& val) {
  if (val) {
    args.emplace_back(flag);
    args.push_back(std::to_string(*val));
  }
}

void add_opt_bool_true(std::vector<std::string>& args, const char* flag, const std::optional<bool>& val) {
  if (val && *val) {
    args.emplace_back(flag);
  }
}

auto logging_config_from_toml(const toml::table& config) -> LoggingConfig {
  auto logging = LoggingConfig{};

  const auto logging_node = config["logging"];
  if (!logging_node) {
    return logging;
  }

  const auto* logging_table = logging_node.as_table();
  if (logging_table == nullptr) {
    throw std::runtime_error("[logging] must be a TOML table");
  }

  if (const auto rotate_size = opt_int(*logging_table, "rotate_size")) {
    if (*rotate_size <= 0) {
      throw std::runtime_error("[logging].rotate_size must be greater than 0");
    }
    logging.rotate_size = static_cast<std::uint64_t>(*rotate_size);
  }

  if (const auto retain_files = opt_int(*logging_table, "retain_files")) {
    if (*retain_files <= 0) {
      throw std::runtime_error("[logging].retain_files must be greater than 0");
    }
    logging.retain_files = static_cast<std::uint64_t>(*retain_files);
  }

  return logging;
}

auto utc_start_timestamp() -> std::string {
  const auto now = std::time(nullptr);
  std::tm utc_time{};
  gmtime_r(&now, &utc_time);

  std::array<char, 32> buffer{};
  if (std::strftime(buffer.data(), buffer.size(), "%Y%m%dT%H%M%SZ", &utc_time) == 0) {
    throw std::runtime_error("Failed to format launcher UTC start time");
  }
  return buffer.data();
}

auto log_path_for(const std::string& start_timestamp, const std::string& module_name) -> std::string {
  return (std::filesystem::path{"log"} / (start_timestamp + "-" + module_name + ".log")).string();
}

void append_logging_args(std::vector<std::string>& args, const std::string& start_timestamp,
                         const std::string& module_name, std::uint64_t rotate_size) {
  args.emplace_back("--log-file");
  args.push_back(log_path_for(start_timestamp, module_name));
  args.emplace_back("--log-rotate-size");
  args.push_back(std::to_string(rotate_size));
}

auto is_log_file(const std::filesystem::path& path) -> bool {
  const auto filename = path.filename().string();
  return filename.find(".log") != std::string::npos;
}

void cleanup_old_log_files(std::uint64_t retain_files) {
  namespace fs = std::filesystem;

  const auto log_dir = fs::path{"log"};
  fs::create_directories(log_dir);

  struct LogFile {
    fs::path path;
    fs::file_time_type modified_time;
  };

  std::vector<LogFile> log_files;
  std::error_code error;
  auto it = fs::directory_iterator{log_dir, error};
  if (error) {
    spdlog::warn("[launcher] failed to scan log directory '{}': {}", log_dir.string(), error.message());
    return;
  }

  const auto end = fs::directory_iterator{};
  for (; it != end; it.increment(error)) {
    if (error) {
      spdlog::warn("[launcher] failed while scanning log directory '{}': {}", log_dir.string(), error.message());
      return;
    }

    const auto& entry = *it;
    if (!entry.is_regular_file(error) || error || !is_log_file(entry.path())) {
      error.clear();
      continue;
    }

    const auto modified_time = entry.last_write_time(error);
    if (error) {
      spdlog::warn("[launcher] failed to read log mtime '{}': {}", entry.path().string(), error.message());
      error.clear();
      continue;
    }
    log_files.push_back(LogFile{.path = entry.path(), .modified_time = modified_time});
  }

  if (log_files.size() <= retain_files) {
    return;
  }

  std::sort(log_files.begin(), log_files.end(),
            [](const LogFile& lhs, const LogFile& rhs) { return lhs.modified_time < rhs.modified_time; });

  const auto remove_count = log_files.size() - static_cast<std::size_t>(retain_files);
  for (std::size_t i = 0; i < remove_count; ++i) {
    fs::remove(log_files[i].path, error);
    if (error) {
      spdlog::warn("[launcher] failed to remove old log '{}': {}", log_files[i].path.string(), error.message());
      error.clear();
    } else {
      spdlog::info("[launcher] removed old log '{}'", log_files[i].path.string());
    }
  }
}

} // namespace

static auto build_state_machine_args(const toml::table& /* cfg */) -> std::vector<std::string> {
  using namespace signlang::launcher::ipc;
  return {
    kExeStateMachine,
    "--state-event-service",      kStateEvent,
    "--state-blackboard-service", kStateBlackboard,
    "--state-control-service",    kStateControl,
  };
}

static auto build_audio_frontend_args(const toml::table& cfg) -> std::vector<std::string> {
  using namespace signlang::launcher::ipc;
  std::vector<std::string> args = {
    kExeAudioFrontend,
    "--service", kAudioService,
    "--localization-blackboard", kAudioLocalizationBlackboard,
  };

  if (const auto* tbl = cfg["audio_frontend"].as_table()) {
    add_opt_str(args, "--device", opt_string(*tbl, "device"));
    add_opt_int(args, "--capture-rate", opt_int(*tbl, "capture_rate"));
    add_opt_int(args, "--capture-channels", opt_int(*tbl, "capture_channels"));
    add_opt_int(args, "--publish-rate", opt_int(*tbl, "publish_rate"));
    add_opt_int(args, "--publish-channels", opt_int(*tbl, "publish_channels"));
    add_opt_int(args, "--period-ms", opt_int(*tbl, "period_ms"));
    add_opt_double(args, "--localization-tdoa-weight", opt_double(*tbl, "localization_tdoa_weight"));
    add_opt_double(args, "--localization-rms-weight", opt_double(*tbl, "localization_rms_weight"));
  }
  return args;
}

static auto build_video_frontend_args(const toml::table& cfg) -> std::vector<std::string> {
  using namespace signlang::launcher::ipc;
  std::vector<std::string> args = {kExeVideoFrontend, "--service", kVideoService};

  if (const auto* tbl = cfg["video_frontend"].as_table()) {
    add_opt_str(args, "--device", opt_string(*tbl, "device"));
    add_opt_int(args, "--capture-width", opt_int(*tbl, "capture_width"));
    add_opt_int(args, "--capture-height", opt_int(*tbl, "capture_height"));
    add_opt_int(args, "--output-width", opt_int(*tbl, "output_width"));
    add_opt_int(args, "--output-height", opt_int(*tbl, "output_height"));
    add_opt_int(args, "--fps", opt_int(*tbl, "fps"));
  }
  return args;
}

static auto build_speech_asr_args(const toml::table& cfg) -> std::vector<std::string> {
  using namespace signlang::launcher::ipc;
  std::vector<std::string> args = {
    kExeSpeechAsr,
    "--input-service",            kAudioService,
    "--output-service",           kSpeechAsrOutput,
    "--state-event-service",      kStateEvent,
    "--state-blackboard-service", kStateBlackboard,
  };

  if (const auto* tbl = cfg["speech_asr"].as_table()) {
    add_opt_str(args, "--language", opt_string(*tbl, "language"));
    add_opt_int(args, "--window-ms", opt_int(*tbl, "window_ms"));
    add_opt_double(args, "--overlap", opt_double(*tbl, "overlap"));
    add_opt_int(args, "--max-decode-steps", opt_int(*tbl, "max_decode_steps"));
    add_opt_str(args, "--encoder-model", opt_string(*tbl, "encoder_model"));
    add_opt_str(args, "--decoder-model", opt_string(*tbl, "decoder_model"));
    add_opt_str(args, "--vocab-en", opt_string(*tbl, "vocab_en"));
    add_opt_str(args, "--vocab-zh", opt_string(*tbl, "vocab_zh"));
    add_opt_str(args, "--mel-filters", opt_string(*tbl, "mel_filters"));
    add_opt_str(args, "--npu-core", opt_string(*tbl, "npu_core"));
    add_opt_str(args, "--encoder-npu-core", opt_string(*tbl, "encoder_npu_core"));
    add_opt_str(args, "--decoder-npu-core", opt_string(*tbl, "decoder_npu_core"));
    add_opt_str(args, "--rknn-priority", opt_string(*tbl, "rknn_priority"));
    add_opt_int(args, "--subscriber-buffer", opt_int(*tbl, "subscriber_buffer"));
  }
  return args;
}

static auto build_env_sound_det_args(const toml::table& cfg) -> std::vector<std::string> {
  using namespace signlang::launcher::ipc;
  std::vector<std::string> args = {
    kExeEnvSoundDet,
    "--input-service",         kAudioService,
    "--state-control-service", kStateControl,
  };

  if (const auto* tbl = cfg["env_sound_det"].as_table()) {
    add_opt_int(args, "--window-ms", opt_int(*tbl, "window_ms"));
    add_opt_double(args, "--overlap", opt_double(*tbl, "overlap"));
    add_opt_double(args, "--score-threshold", opt_double(*tbl, "score_threshold"));
    add_opt_str(args, "--model", opt_string(*tbl, "model"));
    add_opt_str(args, "--class-map", opt_string(*tbl, "class_map"));
    add_opt_str(args, "--npu-core", opt_string(*tbl, "npu_core"));
    add_opt_str(args, "--rknn-priority", opt_string(*tbl, "rknn_priority"));
    add_opt_int(args, "--subscriber-buffer", opt_int(*tbl, "subscriber_buffer"));
  }
  return args;
}

static auto build_handpose_det_args(const toml::table& cfg) -> std::vector<std::string> {
  using namespace signlang::launcher::ipc;
  std::vector<std::string> args = {
    kExeHandposeDet,
    "--input-service",            kVideoService,
    "--output-service",           kHandposeOutput,
    "--state-event-service",      kStateEvent,
    "--state-blackboard-service", kStateBlackboard,
  };

  if (const auto* tbl = cfg["handpose_det"].as_table()) {
    add_opt_str(args, "--model", opt_string(*tbl, "model"));
    add_opt_double(args, "--confidence", opt_double(*tbl, "confidence"));
    add_opt_double(args, "--nms", opt_double(*tbl, "nms"));
    add_opt_int(args, "--keypoints", opt_int(*tbl, "keypoints"));
    add_opt_int(args, "--max-detections", opt_int(*tbl, "max_detections"));
    add_opt_str(args, "--npu-core", opt_string(*tbl, "npu_core"));
    add_opt_int(args, "--subscriber-buffer", opt_int(*tbl, "subscriber_buffer"));
  }
  return args;
}

static auto build_signlang_det_args(const toml::table& cfg) -> std::vector<std::string> {
  using namespace signlang::launcher::ipc;
  std::vector<std::string> args = {
    kExeSignlangDet,
    "--input-service",            kHandposeOutput,
    "--output-service",           kSignlangOutput,
    "--state-event-service",      kStateEvent,
    "--state-blackboard-service", kStateBlackboard,
  };

  if (const auto* tbl = cfg["signlang_det"].as_table()) {
    add_opt_str(args, "--model", opt_string(*tbl, "model"));
    add_opt_str(args, "--label-map", opt_string(*tbl, "label_map"));
    add_opt_str(args, "--prototypes", opt_string(*tbl, "prototypes"));
    add_opt_int(args, "--sequence-length", opt_int(*tbl, "sequence_length"));
    add_opt_double(args, "--overlap-ratio", opt_double(*tbl, "overlap_ratio"));
    add_opt_double(args, "--min-confidence", opt_double(*tbl, "min_confidence"));
    add_opt_double(args, "--motion-weight", opt_double(*tbl, "motion_weight"));
    add_opt_double(args, "--dtw-window-ratio", opt_double(*tbl, "dtw_window_ratio"));
    add_opt_double(args, "--confidence-threshold", opt_double(*tbl, "confidence_threshold"));
    add_opt_double(args, "--confidence-margin", opt_double(*tbl, "confidence_margin"));
    add_opt_str(args, "--npu-core", opt_string(*tbl, "npu_core"));
    add_opt_int(args, "--subscriber-buffer", opt_int(*tbl, "subscriber_buffer"));
  }
  return args;
}

auto main(int argc, char** argv) -> int {
  using signlang::launcher::ProgramOptions;
  using signlang::launcher::ProgramUsage;
  using signlang::launcher::parse_program_options;

  signlang::logging::initialize();

  try {
    const auto parse_result = parse_program_options(argc, argv);
    if (std::holds_alternative<ProgramUsage>(parse_result)) {
      std::cout << std::get<ProgramUsage>(parse_result).text << '\n';
      return 0;
    }

    const auto& options = std::get<ProgramOptions>(parse_result);

    toml::table config;
    try {
      config = toml::parse_file(options.config_path);
    } catch (const toml::parse_error& err) {
      spdlog::error("[launcher] failed to parse config file '{}': {}", options.config_path, err.what());
      return 1;
    } catch (const std::runtime_error& err) {
      spdlog::error("[launcher] failed to open config file '{}': {}", options.config_path, err.what());
      return 1;
    }

    const auto logging_config = logging_config_from_toml(config);
    const auto start_timestamp = utc_start_timestamp();
    signlang::logging::initialize(
        signlang::logging::Options{
            .log_file = log_path_for(start_timestamp, "launcher"),
            .rotate_size = logging_config.rotate_size,
        },
        logging_config.retain_files);
    cleanup_old_log_files(logging_config.retain_files);

    spdlog::info("[launcher] loaded config: {}", options.config_path);

    // Warn about any IPC service keys in the TOML
    warn_ipc_keys_in_config(config);

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    std::signal(SIGCHLD, handle_signal);

    struct ModuleEntry {
      std::string name;
      std::vector<std::string> args;
    };

    std::vector<ModuleEntry> modules = {
      {"state_machine",  build_state_machine_args(config)},
      {"audio_frontend", build_audio_frontend_args(config)},
      {"video_frontend", build_video_frontend_args(config)},
      {"speech_asr",     build_speech_asr_args(config)},
      {"env_sound_det",  build_env_sound_det_args(config)},
      {"handpose_det",   build_handpose_det_args(config)},
      {"signlang_det",   build_signlang_det_args(config)},
    };

    for (auto& mod : modules) {
      append_logging_args(mod.args, start_timestamp, mod.name, logging_config.rotate_size);
    }

    for (const auto& mod : modules) {
      auto args_text = std::string{};
      for (std::size_t i = 1; i < mod.args.size(); ++i) {
        if (!args_text.empty()) {
          args_text += ' ';
        }
        args_text += mod.args[i];
      }
      spdlog::info("[launcher] starting {} {}", mod.name, args_text);

      const auto pid = launch_child(mod.args);
      if (pid < 0) {
        spdlog::error("[launcher] failed to start {}, aborting", mod.name);
        terminate_all_children();
        return 1;
      }

      g_children.push_back({pid, mod.name});
      spdlog::info("[launcher] {} started (pid {})", mod.name, pid);
    }

    spdlog::info("[launcher] all {} modules running, monitoring...", g_children.size());

    while (g_shutdown == 0) {
      int status = 0;
      const auto pid = waitpid(-1, &status, WNOHANG);

      if (pid > 0) {
        auto it = std::find_if(g_children.begin(), g_children.end(),
                               [pid](const ChildInfo& c) { return c.pid == pid; });
        if (it != g_children.end()) {
          if (WIFEXITED(status)) {
            const auto exit_code = WEXITSTATUS(status);
            if (exit_code != 0) {
              spdlog::error("[launcher] {} (pid {}) exited with code {}, shutting down",
                            it->name, pid, exit_code);
            } else {
              spdlog::warn("[launcher] {} (pid {}) exited normally, shutting down", it->name, pid);
            }
          } else if (WIFSIGNALED(status)) {
            spdlog::error("[launcher] {} (pid {}) killed by signal {}, shutting down",
                          it->name, pid, WTERMSIG(status));
          }
          g_children.erase(it);
        }
        g_shutdown = 1;
      } else if (pid == 0) {
        struct timespec ts{ .tv_sec = 0, .tv_nsec = 500000000 };
        nanosleep(&ts, nullptr);
      } else {
        break;
      }
    }

    terminate_all_children();
    return 0;
  } catch (const std::exception& error) {
    spdlog::error("[launcher] fatal error: {}", error.what());
    return 1;
  }
}
