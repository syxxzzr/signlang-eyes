#include "program_options.hpp"

#include "common/runtime.hpp"
#include "spdlog/spdlog.h"
#include "toml.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <sys/wait.h>
#include <system_error>
#include <unistd.h>
#include <variant>
#include <vector>

namespace signlang::launcher::ipc {

  constexpr auto kAudioService = "audio_capture";
  constexpr auto kVideoService = "video_capture";
  constexpr auto kSpeechAsrOutput = "speech_asr_result";
  constexpr auto kHandposeOutput = "handpose_result";
  constexpr auto kSignlangOutput = "signlang_result";
  constexpr auto kSignlangPrototypeControl = "signlang_prototype_control";
  constexpr auto kStateEvent = "app_state_event";
  constexpr auto kStateBlackboard = "app_state_blackboard";
  constexpr auto kStateControl = "app_state_control";
  constexpr auto kAudioLocalizationBlackboard = "audio_source_localization";

} // namespace signlang::launcher::ipc

namespace {

  constexpr auto kExeStateMachine = "bin/state_machine";
  constexpr auto kExeAudioFrontend = "bin/audio_frontend";
  constexpr auto kExeVideoFrontend = "bin/video_frontend";
  constexpr auto kExeSpeechAsr = "bin/speech_asr";
  constexpr auto kExeEnvSoundDet = "bin/env_sound_det";
  constexpr auto kExeHandposeDet = "bin/handpose_det";
  constexpr auto kExeSignlangManager = "bin/signlang_manager";
  constexpr auto kExeSignlangDet = "bin/signlang_det";

  constexpr std::array kIpcKeys = {
      "input_service",         "input-service",         "output_service",           "output-service",
      "state_event_service",   "state-event-service",   "state_blackboard_service", "state-blackboard-service",
      "state_control_service", "state-control-service", "localization_blackboard",  "localization-blackboard",
  };

  void warn_ipc_keys_in_table(const toml::table& tbl, const std::string& section_name) {
    for (const auto& [key, node] : tbl) {
      for (const auto* ipc_key : kIpcKeys) {
        if (key == ipc_key) {
          spdlog::warn("[{}] '{}' is ignored; IPC service names are hardcoded in the launcher", section_name,
                       key.str());
          break;
        }
      }
    }
  }

  void warn_ipc_keys_in_config(const toml::table& config) {
    constexpr std::array kSections = {
        "state_machine", "audio_frontend", "video_frontend",   "speech_asr",
        "env_sound_det", "handpose_det",   "signlang_manager", "signlang_det",
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

  struct LauncherConfig {
    std::int64_t restart_attempts = -1;
  };

  struct ModuleEntry {
    std::string name;
    std::vector<std::string> args;
  };

  std::vector<ChildInfo> g_children;

  void terminate_all_children() {
    for (const auto& child : g_children) {
      spdlog::info("terminating {} (pid {})", child.name, child.pid);
      kill(child.pid, SIGTERM);
    }
    for (const auto& child : g_children) {
      int status = 0;
      waitpid(child.pid, &status, 0);
    }
    g_children.clear();
  }

  auto launch_child(const std::vector<std::string>& args) -> pid_t {
    int pipefd[2];
    if (pipe2(pipefd, O_CLOEXEC) < 0) {
      spdlog::error("pipe2 failed: {}", std::strerror(errno));
      return -1;
    }

    const auto pid = fork();
    if (pid < 0) {
      spdlog::error("fork failed: {}", std::strerror(errno));
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
      spdlog::error("exec failed for {}: {}", args[0], std::strerror(child_err));
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

  void add_opt_bool_assignment(std::vector<std::string>& args, const char* flag, const std::optional<bool>& val) {
    if (val) {
      args.emplace_back(std::string{flag} + "=" + (*val ? "true" : "false"));
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

  auto launcher_config_from_toml(const toml::table& config) -> LauncherConfig {
    auto launcher = LauncherConfig{};

    const auto launcher_node = config["launcher"];
    if (!launcher_node) {
      return launcher;
    }

    const auto* launcher_table = launcher_node.as_table();
    if (launcher_table == nullptr) {
      throw std::runtime_error("launcher section must be a TOML table");
    }

    if (const auto restart_attempts = opt_int(*launcher_table, "restart_attempts")) {
      launcher.restart_attempts = *restart_attempts;
    }

    return launcher;
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
      spdlog::warn("failed to scan log directory '{}': {}", log_dir.string(), error.message());
      return;
    }

    const auto end = fs::directory_iterator{};
    for (; it != end; it.increment(error)) {
      if (error) {
        spdlog::warn("failed while scanning log directory '{}': {}", log_dir.string(), error.message());
        return;
      }

      const auto& entry = *it;
      if (!entry.is_regular_file(error) || error || !is_log_file(entry.path())) {
        error.clear();
        continue;
      }

      const auto modified_time = entry.last_write_time(error);
      if (error) {
        spdlog::warn("failed to read log mtime '{}': {}", entry.path().string(), error.message());
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
        spdlog::warn("failed to remove old log '{}': {}", log_files[i].path.string(), error.message());
        error.clear();
      } else {
        spdlog::info("removed old log '{}'", log_files[i].path.string());
      }
    }
  }

  auto resolve_config_path(const std::string& config_path, const std::filesystem::path& invocation_cwd)
      -> std::filesystem::path {
    const auto path = std::filesystem::path{config_path};
    if (path.is_absolute()) {
      return path;
    }

    const auto invocation_path = invocation_cwd / path;
    std::error_code error;
    if (std::filesystem::exists(invocation_path, error) && !error) {
      return invocation_path;
    }

    return path;
  }

  void sleep_before_restart() {
    struct timespec ts {
      .tv_sec = 1, .tv_nsec = 0
    };
    nanosleep(&ts, nullptr);
  }

  auto can_restart(std::int64_t restart_attempts, std::int64_t completed_restarts) -> bool {
    return restart_attempts < 0 || completed_restarts < restart_attempts;
  }

} // namespace

static auto build_state_machine_args(const toml::table& cfg) -> std::vector<std::string> {
  using namespace signlang::launcher::ipc;
  std::vector<std::string> args = {
      kExeStateMachine, "--state-event-service",   kStateEvent,   "--state-blackboard-service",
      kStateBlackboard, "--state-control-service", kStateControl,
  };

  if (const auto* tbl = cfg["state_machine"].as_table()) {
    add_opt_str(args, "--initial-state", opt_string(*tbl, "initial_state"));
  }

  return args;
}

static auto build_audio_frontend_args(const toml::table& cfg) -> std::vector<std::string> {
  using namespace signlang::launcher::ipc;
  std::vector<std::string> args = {
      kExeAudioFrontend, "--service", kAudioService, "--localization-blackboard", kAudioLocalizationBlackboard,
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
      kExeSpeechAsr, "--input-service", kAudioService, "--output-service", kSpeechAsrOutput,
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
      kExeEnvSoundDet, "--input-service", kAudioService, "--state-control-service", kStateControl,
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
      kExeHandposeDet, "--input-service", kVideoService, "--output-service", kHandposeOutput,
  };

  if (const auto* tbl = cfg["handpose_det"].as_table()) {
    add_opt_str(args, "--model", opt_string(*tbl, "model"));
    add_opt_str(args, "--landmark-model", opt_string(*tbl, "landmark_model"));
    add_opt_double(args, "--confidence", opt_double(*tbl, "confidence"));
    add_opt_double(args, "--presence-threshold", opt_double(*tbl, "presence_threshold"));
    add_opt_double(args, "--tracking-threshold", opt_double(*tbl, "tracking_threshold"));
    add_opt_double(args, "--nms-iou-threshold", opt_double(*tbl, "nms_iou_threshold"));
    add_opt_double(args, "--tracking-iou-match", opt_double(*tbl, "tracking_iou_match"));
    add_opt_double(args, "--crop-expansion", opt_double(*tbl, "crop_expansion"));
    add_opt_double(args, "--base-roi-expansion", opt_double(*tbl, "base_roi_expansion"));
    add_opt_double(args, "--small-hand-expansion", opt_double(*tbl, "small_hand_expansion"));
    add_opt_double(args, "--large-hand-expansion", opt_double(*tbl, "large_hand_expansion"));
    add_opt_double(args, "--small-hand-threshold", opt_double(*tbl, "small_hand_threshold"));
    add_opt_double(args, "--large-hand-threshold", opt_double(*tbl, "large_hand_threshold"));
    add_opt_double(args, "--boundary-margin", opt_double(*tbl, "boundary_margin"));
    add_opt_double(args, "--boundary-min-factor", opt_double(*tbl, "boundary_min_factor"));
    add_opt_double(args, "--euro-min-cutoff", opt_double(*tbl, "euro_min_cutoff"));
    add_opt_double(args, "--euro-beta", opt_double(*tbl, "euro_beta"));
    add_opt_double(args, "--euro-d-cutoff", opt_double(*tbl, "euro_d_cutoff"));
    add_opt_double(args, "--handedness-threshold", opt_double(*tbl, "handedness_threshold"));
    add_opt_int(args, "--max-tracking-gap", opt_int(*tbl, "max_tracking_gap"));
    add_opt_int(args, "--max-stale-frames", opt_int(*tbl, "max_stale_frames"));
    add_opt_bool_assignment(args, "--single-hand", opt_bool(*tbl, "single_hand"));
    add_opt_str(args, "--npu-core", opt_string(*tbl, "npu_core"));
    add_opt_str(args, "--palm-npu-core", opt_string(*tbl, "palm_npu_core"));
    add_opt_str(args, "--landmark-npu-core", opt_string(*tbl, "landmark_npu_core"));
    add_opt_int(args, "--subscriber-buffer", opt_int(*tbl, "subscriber_buffer"));
  }
  return args;
}

static auto build_signlang_det_args(const toml::table& cfg) -> std::vector<std::string> {
  using namespace signlang::launcher::ipc;
  std::vector<std::string> args = {
      kExeSignlangDet,
      "--input-service",
      kHandposeOutput,
      "--output-service",
      kSignlangOutput,
      "--prototype-control-service",
      kSignlangPrototypeControl,
  };

  if (const auto* tbl = cfg["signlang_det"].as_table()) {
    add_opt_str(args, "--model", opt_string(*tbl, "model"));
    add_opt_str(args, "--prototypes", opt_string(*tbl, "prototypes"));
    add_opt_int(args, "--sequence-length", opt_int(*tbl, "sequence_length"));
    add_opt_double(args, "--overlap-ratio", opt_double(*tbl, "overlap_ratio"));
    add_opt_double(args, "--min-confidence", opt_double(*tbl, "min_confidence"));
    add_opt_double(args, "--motion-weight", opt_double(*tbl, "motion_weight"));
    add_opt_double(args, "--dtw-window-ratio", opt_double(*tbl, "dtw_window_ratio"));
    add_opt_double(args, "--confidence-threshold", opt_double(*tbl, "confidence_threshold"));
    add_opt_double(args, "--confidence-margin", opt_double(*tbl, "confidence_margin"));
    add_opt_int(args, "--duplicate-suppression-ms", opt_int(*tbl, "duplicate_suppression_ms"));
    add_opt_str(args, "--npu-core", opt_string(*tbl, "npu_core"));
    add_opt_int(args, "--subscriber-buffer", opt_int(*tbl, "subscriber_buffer"));
  }
  return args;
}

static auto build_signlang_manager_args(const toml::table& cfg) -> std::vector<std::string> {
  using namespace signlang::launcher::ipc;
  std::vector<std::string> args = {
      kExeSignlangManager,
      "--input-service",
      kHandposeOutput,
      "--signlang-result-service",
      kSignlangOutput,
      "--signlang-control-service",
      kSignlangPrototypeControl,
  };

  if (const auto* tbl = cfg["signlang_manager"].as_table()) {
    add_opt_str(args, "--bluetooth-name", opt_string(*tbl, "bluetooth_name"));
    add_opt_str(args, "--adapter-path", opt_string(*tbl, "adapter_path"));
    add_opt_str(args, "--model", opt_string(*tbl, "model"));
    add_opt_str(args, "--prototypes", opt_string(*tbl, "prototypes"));
    add_opt_double(args, "--min-confidence", opt_double(*tbl, "min_confidence"));
    add_opt_double(args, "--motion-weight", opt_double(*tbl, "motion_weight"));
    add_opt_double(args, "--upload-window-overlap", opt_double(*tbl, "upload_window_overlap"));
    add_opt_int(args, "--subscriber-buffer", opt_int(*tbl, "subscriber_buffer"));
    add_opt_int(args, "--stream-fps", opt_int(*tbl, "stream_fps"));
    add_opt_int(args, "--max-notify-payload", opt_int(*tbl, "max_notify_payload"));
    add_opt_str(args, "--npu-core", opt_string(*tbl, "npu_core"));
    add_opt_bool_true(args, "--enable-streaming-by-default", opt_bool(*tbl, "enable_streaming_by_default"));
  }

  return args;
}

static auto build_modules(const toml::table& config) -> std::vector<ModuleEntry> {
  return {
      {"state_machine", build_state_machine_args(config)},
      {"audio_frontend", build_audio_frontend_args(config)},
      {"video_frontend", build_video_frontend_args(config)},
      {"speech_asr", build_speech_asr_args(config)},
      {"env_sound_det", build_env_sound_det_args(config)},
      {"handpose_det", build_handpose_det_args(config)},
      {"signlang_manager", build_signlang_manager_args(config)},
      {"signlang_det", build_signlang_det_args(config)},
  };
}

static auto run_modules_once(const std::vector<ModuleEntry>& modules) -> bool {
  g_children.clear();

  for (const auto& mod : modules) {
    auto args_text = std::string{};
    for (std::size_t i = 1; i < mod.args.size(); ++i) {
      if (!args_text.empty()) {
        args_text += ' ';
      }
      args_text += mod.args[i];
    }
    spdlog::info("starting {} {}", mod.name, args_text);

    const auto pid = launch_child(mod.args);
    if (pid < 0) {
      spdlog::error("failed to start {}", mod.name);
      terminate_all_children();
      return false;
    }

    g_children.push_back({pid, mod.name});
    spdlog::info("{} started (pid {})", mod.name, pid);
  }

  spdlog::info("all {} modules running, monitoring...", g_children.size());

  while (!signlang::runtime::shutdown_requested()) {
    int status = 0;
    const auto pid = waitpid(-1, &status, WNOHANG);

    if (pid > 0) {
      auto it = std::find_if(g_children.begin(), g_children.end(), [pid](const ChildInfo& c) { return c.pid == pid; });
      if (it != g_children.end()) {
        if (WIFEXITED(status)) {
          const auto exit_code = WEXITSTATUS(status);
          if (exit_code != 0) {
            spdlog::error("{} (pid {}) exited with code {}", it->name, pid, exit_code);
          } else {
            spdlog::warn("{} (pid {}) exited normally", it->name, pid);
          }
        } else if (WIFSIGNALED(status)) {
          spdlog::error("{} (pid {}) killed by signal {}", it->name, pid, WTERMSIG(status));
        }
        g_children.erase(it);
      }

      terminate_all_children();
      return false;
    }

    if (pid == 0) {
      struct timespec ts{.tv_sec = 0, .tv_nsec = 500000000};
      nanosleep(&ts, nullptr);
      continue;
    }

    if (errno == ECHILD) {
      break;
    }

    spdlog::error("waitpid failed: {}", std::strerror(errno));
    terminate_all_children();
    return false;
  }

  terminate_all_children();
  return true;
}

auto main(int argc, char** argv) -> int {
  using signlang::launcher::parse_program_options;
  using signlang::launcher::ProgramOptions;
  using signlang::launcher::ProgramUsage;

  signlang::logging::initialize({}, signlang::logging::kDefaultRetainFiles, "launcher");

  try {
    const auto parse_result = parse_program_options(argc, argv);
    if (std::holds_alternative<ProgramUsage>(parse_result)) {
      std::cout << std::get<ProgramUsage>(parse_result).text << '\n';
      return 0;
    }

    const auto& options = std::get<ProgramOptions>(parse_result);
    const auto invocation_cwd = std::filesystem::current_path();
    signlang::runtime::enter_runtime_root();
    const auto config_path = resolve_config_path(options.config_path, invocation_cwd);

    toml::table config;
    try {
      config = toml::parse_file(config_path.string());
    } catch (const toml::parse_error& err) {
      spdlog::error("failed to parse config file '{}': {}", config_path.string(), err.what());
      return 1;
    } catch (const std::runtime_error& err) {
      spdlog::error("failed to open config file '{}': {}", config_path.string(), err.what());
      return 1;
    }

    const auto logging_config = logging_config_from_toml(config);
    const auto launcher_config = launcher_config_from_toml(config);
    const auto start_timestamp = utc_start_timestamp();
    signlang::logging::initialize(
        signlang::logging::Options{
            .log_file = log_path_for(start_timestamp, "launcher"),
            .rotate_size = logging_config.rotate_size,
        },
        logging_config.retain_files, "launcher");
    cleanup_old_log_files(logging_config.retain_files);

    spdlog::info("loaded config: {}", config_path.string());

    // Warn about any IPC service keys in the TOML
    warn_ipc_keys_in_config(config);

    signlang::runtime::install_shutdown_signal_handlers();
    std::signal(SIGCHLD, SIG_DFL);

    auto modules = build_modules(config);
    for (auto& mod : modules) {
      append_logging_args(mod.args, start_timestamp, mod.name, logging_config.rotate_size);
    }

    auto completed_restarts = std::int64_t{0};
    while (!signlang::runtime::shutdown_requested()) {
      const auto normal_shutdown = run_modules_once(modules);
      if (normal_shutdown || signlang::runtime::shutdown_requested()) {
        return 0;
      }

      if (!can_restart(launcher_config.restart_attempts, completed_restarts)) {
        spdlog::error("module startup/runtime failed; restart limit {} reached",
                      launcher_config.restart_attempts);
        return 1;
      }

      ++completed_restarts;
      spdlog::warn("module startup/runtime failed; restarting system (attempt {}, limit {})",
                   completed_restarts, launcher_config.restart_attempts < 0 ? std::string{"unlimited"}
                                                                            : std::to_string(launcher_config.restart_attempts));
      sleep_before_restart();
    }
    return 0;
  } catch (const std::exception& error) {
    spdlog::error("fatal error: {}", error.what());
    return 1;
  }
}
