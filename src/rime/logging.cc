//
// Copyright RIME Developers
// Distributed under the BSD License
//

#include <rime/build_config.h>

#ifdef RIME_ENABLE_LOGGING

#include "rime/logging.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#ifdef __ANDROID__
#include <spdlog/sinks/android_sink.h>
#endif

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace rime {
namespace {

namespace fs = std::filesystem;

#ifdef RIME_ALSO_LOG_TO_STDERR
constexpr bool kAlsoLogToStderr = true;
#else
constexpr bool kAlsoLogToStderr = false;
#endif

std::mutex g_mutex;
std::shared_ptr<spdlog::logger> g_logger;
bool g_initialized = false;
bool g_log_to_stderr = true;
std::vector<std::string> g_logging_directories;
std::atomic<int> g_vlog_level{0};

spdlog::level::level_enum ToSpdlogLevel(LogSeverity severity) {
  switch (severity) {
    case LogSeverity::Info:
      return spdlog::level::info;
    case LogSeverity::Warning:
      return spdlog::level::warn;
    case LogSeverity::Error:
      return spdlog::level::err;
    case LogSeverity::Fatal:
      return spdlog::level::critical;
  }
  return spdlog::level::info;
}

spdlog::level::level_enum ToSpdlogLevel(int min_log_level) {
  if (min_log_level <= 0)
    return spdlog::level::info;
  if (min_log_level == 1)
    return spdlog::level::warn;
  if (min_log_level == 2)
    return spdlog::level::err;
  return spdlog::level::critical;
}

char LevelChar(LogSeverity severity) {
  switch (severity) {
    case LogSeverity::Info:
      return 'I';
    case LogSeverity::Warning:
      return 'W';
    case LogSeverity::Error:
      return 'E';
    case LogSeverity::Fatal:
      return 'F';
  }
  return 'I';
}

std::string ToUtf8(const fs::path& path) {
#if defined(__cpp_lib_char8_t)
  const auto value = path.u8string();
  return std::string(value.begin(), value.end());
#else
  return path.u8string();
#endif
}

fs::path ToPath(const std::string& utf8_path) {
#ifdef _WIN32
#if defined(__cpp_lib_char8_t)
  // u8path() is deprecated in C++20; the path constructor accepts u8string.
  return fs::path(std::u8string(utf8_path.begin(), utf8_path.end()));
#else
  return fs::u8path(utf8_path);
#endif
#else
  return fs::path(utf8_path);
#endif
}

std::string CurrentDate() {
  std::time_t now = std::time(nullptr);
  std::tm tm = {};
#ifdef _WIN32
  localtime_s(&tm, &now);
#else
  localtime_r(&now, &tm);
#endif
  char buffer[16] = {};
  std::strftime(buffer, sizeof(buffer), "%Y%m%d", &tm);
  return buffer;
}

std::string CurrentPid() {
#ifdef _WIN32
  return std::to_string(_getpid());
#else
  return std::to_string(getpid());
#endif
}

std::vector<fs::path> CandidateDirectories(const char* log_dir) {
  std::vector<fs::path> directories;
  if (log_dir != nullptr && log_dir[0] == '\0')
    return directories;

  if (log_dir != nullptr) {
    directories.push_back(ToPath(log_dir));
    return directories;
  }

  std::error_code ec;
  const auto temp_dir = fs::temp_directory_path(ec);
  if (!ec && !temp_dir.empty())
    directories.push_back(temp_dir);

  const auto current_dir = fs::current_path(ec);
  if (!ec && !current_dir.empty())
    directories.push_back(current_dir);

  std::vector<fs::path> unique_directories;
  for (const auto& directory : directories) {
    if (std::find(unique_directories.begin(), unique_directories.end(),
                  directory) == unique_directories.end()) {
      unique_directories.push_back(directory);
    }
  }
  return unique_directories;
}

struct LogFileSink {
  std::string file_name;
  std::shared_ptr<spdlog::sinks::basic_file_sink_mt> sink;
};

struct SeverityFileSpec {
  const char* name;
  spdlog::level::level_enum level;
};

constexpr SeverityFileSpec kSeverityFiles[] = {
    {"INFO", spdlog::level::info},
    {"WARNING", spdlog::level::warn},
    {"ERROR", spdlog::level::err},
    {"FATAL", spdlog::level::critical},
};

// 日志保留天数；glog EnableLogCleaner 语义（移植自本地加固）。
constexpr int kLogRetentionDays = 7;

// 删除超过保留期的旧日志文件（只看 rime 日志命名形态，不碰目录里别的文件）。
void PurgeExpiredLogs(const fs::path& directory,
                      const std::string& app_name) {
  std::error_code ec;
  if (!fs::is_directory(directory, ec))
    return;
  for (fs::directory_iterator it(directory, ec), end; !ec && it != end;
       it.increment(ec)) {
    const auto& entry = *it;
    if (!entry.is_regular_file(ec))
      continue;
    const auto name = ToUtf8(entry.path().filename());
    if (name.compare(0, app_name.size(), app_name) != 0)
      continue;
    if (name.find(".log") == std::string::npos)
      continue;
    auto write_time = fs::last_write_time(entry.path(), ec);
    if (ec) {
      ec.clear();
      continue;
    }
    // C++17 file_clock 与 system_clock 的转换。
    const auto file_now = fs::file_time_type::clock::now();
    const auto age = std::chrono::duration_cast<std::chrono::hours>(
        file_now - write_time);
    if (age > std::chrono::hours(24 * kLogRetentionDays)) {
      fs::remove(entry.path(), ec);
      ec.clear();
    }
  }
}

bool EnsureLogDirectory(const fs::path& directory) {
  std::error_code ec;
  fs::create_directories(directory, ec);
  std::error_code exists_ec;
  if (ec && !fs::exists(directory, exists_ec)) {
    std::fprintf(stderr, "Rime: failed to create log directory %s: %s\n",
                 ToUtf8(directory).c_str(), ec.message().c_str());
    return false;
  }
  return true;
}

// 懒创建 + 单文件上限的 glog 风格文件 sink。
// - 首条消息到达才真正打开文件：产品以 min_log_level=ERROR 运行时平时零日志
//   文件（glog 本为惰性建文件；facade 若急切创建会让每个进程无条件落 4 个空
//   文件，正是 79592f5c 修过的"平时零日志文件"契约）。
// - 单文件超过 kMaxLogBytes（4MB）后滚动新序号文件（.1/.2 插在 .log 前），防失控
//   日志点写出 GB 级单文件（移植自 FLAGS_max_log_size=32）。
// - 首次打开时补建 0600 权限与 <app>.<LEVEL> symlink（glog 兼容形态）。
void RefreshSymlink(const fs::path& directory,
                    const std::string& app_name,
                    const std::string& severity,
                    const std::string& file_name);

class LazyFileSink : public spdlog::sinks::base_sink<std::mutex> {
 public:
  LazyFileSink(const fs::path& directory, const std::string& app_name,
               const std::string& date, const std::string& pid,
               const std::string& severity)
      : directory_(directory),
        app_name_(app_name),
        date_(date),
        pid_(pid),
        severity_(severity) {}

 protected:
  void sink_it_(const spdlog::details::log_msg& msg) override {
    if (!inner_) {
      OpenFile(0);
      if (!inner_)
        return;  // 打不开文件时静默丢消息；stderr 镜像仍会输出。
    }
    if (bytes_written_ >= kMaxLogBytes) {
      ++sequence_;
      OpenFile(sequence_);
      if (!inner_)
        return;
    }
    inner_->log(msg);
    // 近似记账：payload + 默认全格式行开销；上限判断允许少量偏差。
    bytes_written_ += msg.payload.size() + kLineOverhead;
  }

  void flush_() override {
    if (inner_)
      inner_->flush();
  }

 private:
  void OpenFile(int sequence) {
    std::string suffix;
    if (sequence > 0) {
      suffix = "." + std::to_string(sequence);
    }
    file_name_ = app_name_ + "." + date_ + "." + pid_ + "." + severity_ +
                 suffix + ".log";
    const auto file_path = directory_ / ToPath(file_name_);
    std::error_code ec;
    try {
      inner_ = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
          file_path.string(), false);
      fs::permissions(file_path, fs::perms::owner_read | fs::perms::owner_write,
                      fs::perm_options::replace, ec);
      bytes_written_ = 0;
      if (fs::exists(file_path, ec))
        bytes_written_ = fs::file_size(file_path, ec);
      if (ec)
        bytes_written_ = 0;
      RefreshSymlink(directory_, app_name_, severity_, file_name_);
    } catch (const std::exception& ex) {
      std::fprintf(stderr, "Rime: failed to open log file %s: %s\n",
                   ToUtf8(file_path).c_str(), ex.what());
      inner_.reset();
    }
  }

  static constexpr std::size_t kMaxLogBytes = 4u * 1024u * 1024u;
  static constexpr std::size_t kLineOverhead = 64;

  fs::path directory_;
  std::string app_name_;
  std::string date_;
  std::string pid_;
  std::string severity_;
  std::shared_ptr<spdlog::sinks::basic_file_sink_mt> inner_;
  std::string file_name_;
  std::size_t bytes_written_ = 0;
  int sequence_ = 0;
};

void RefreshSymlink(const fs::path& directory,
                    const std::string& app_name,
                    const std::string& severity,
                    const std::string& file_name) {
  const auto link_path = directory / ToPath(app_name + "." + severity);
  std::error_code ec;
  fs::remove(link_path, ec);

  ec.clear();
  fs::create_symlink(ToPath(file_name), link_path, ec);
#ifdef _WIN32
  if (ec) {
    // Creating symlinks on Windows often requires elevated privileges.
    // A hard link to the current log file is a useful fallback.
    ec.clear();
    fs::create_hard_link(directory / ToPath(file_name), link_path, ec);
  }
#endif
  if (ec) {
    std::fprintf(stderr, "Rime: failed to create log symlink %s: %s\n",
                 ToUtf8(link_path).c_str(), ec.message().c_str());
  }
}

std::shared_ptr<spdlog::logger> BuildLogger(const char* app_name,
                                            int min_log_level,
                                            const char* log_dir,
                                            bool* log_to_stderr,
                                            std::vector<std::string>* dirs) {
  const std::string normalized_app_name =
      (app_name != nullptr && app_name[0] != '\0') ? app_name : "rime";
  std::vector<spdlog::sink_ptr> sinks;
  bool file_logging = false;

  if (log_dir == nullptr || log_dir[0] != '\0') {
    const auto directories = CandidateDirectories(log_dir);
    for (const auto& directory : directories) {
      dirs->push_back(ToUtf8(directory));
    }

    const std::string date = CurrentDate();
    const std::string pid = CurrentPid();
    for (const auto& directory : directories) {
      if (!EnsureLogDirectory(directory))
        continue;
      PurgeExpiredLogs(directory, normalized_app_name);

      for (const auto& spec : kSeverityFiles) {
        auto lazy = std::make_shared<LazyFileSink>(directory,
                                                   normalized_app_name, date,
                                                   pid, spec.name);
        // 同级别及以上写入自身文件（glog LogToAllLogfiles 语义）；
        // 低于 min_log_level 的消息被 logger 整体丢弃，文件永不创建。
        lazy->set_level(spec.level);
        sinks.push_back(std::move(lazy));
      }
      file_logging = true;
      break;
    }

    if (!file_logging) {
#ifdef __ANDROID__
      std::fprintf(stderr,
                   "Rime: no writable log directory found; logging to "
                   "logcat instead.\n");
#else
      std::fprintf(stderr,
                   "Rime: no writable log directory found; logging to "
                   "stderr instead.\n");
#endif
    }
  }

#ifdef __ANDROID__
  sinks.push_back(std::make_shared<spdlog::sinks::android_sink_mt>(
      normalized_app_name, true));
#endif

#ifndef __ANDROID__
  // glog mirrored ERROR and FATAL messages to stderr by default
  // (--stderrthreshold=ERROR) even when a log file was in use.  Keep that
  // behavior unless all messages already go to stderr.
  auto stderr_sink = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
  if (file_logging && !kAlsoLogToStderr) {
    stderr_sink->set_level(spdlog::level::err);
  }
  sinks.push_back(stderr_sink);
#else
  if (kAlsoLogToStderr) {
    sinks.push_back(std::make_shared<spdlog::sinks::stderr_color_sink_mt>());
  }
#endif

  auto logger = std::make_shared<spdlog::logger>(normalized_app_name,
                                                 sinks.begin(), sinks.end());
  logger->set_level(ToSpdlogLevel(min_log_level));

  // Use spdlog's default full pattern:
  //   [YYYY-MM-DD HH:MM:SS.mmm] [logger] [level] [source:line] message
  logger->set_pattern("%+");

  // glog flushes WARNING and above immediately (--logbuflevel defaults to 0).
  logger->flush_on(spdlog::level::warn);
  *log_to_stderr = !file_logging;
  return logger;
}

}  // namespace

LogMessage::LogMessage(LogSeverity severity, const char* file, int line)
    : severity_(severity), file_(file), line_(line) {}

LogMessage::~LogMessage() {
  LogMessageToSink(severity_, file_, line_, stream_.str());
}

void InitializeLogging(const char* app_name,
                       int min_log_level,
                       const char* log_dir) {
  bool already_initialized = false;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_initialized) {
      already_initialized = true;
    } else {
      g_logging_directories.clear();
      bool log_to_stderr = true;
      auto logger = BuildLogger(app_name, min_log_level, log_dir,
                                &log_to_stderr, &g_logging_directories);
      g_logger = std::move(logger);
      g_log_to_stderr = log_to_stderr;
      g_initialized = true;
    }
  }

  if (already_initialized) {
    LogMessageToSink(LogSeverity::Warning, __FILE__, __LINE__,
                     "Rime logging is already initialized.");
  }
}

void ShutdownLogging() {
  std::shared_ptr<spdlog::logger> logger;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    logger = g_logger;
    g_logger.reset();
    g_initialized = false;
    g_log_to_stderr = true;
    g_logging_directories.clear();
  }
  if (logger) {
    try {
      logger->flush();
    } catch (const std::exception& ex) {
      std::fprintf(stderr, "Rime: failed to flush logs: %s\n", ex.what());
    } catch (...) {
      std::fprintf(stderr, "Rime: failed to flush logs.\n");
    }
  }
}

bool IsLoggingInitialized() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_initialized;
}

bool IsLogToStderr() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_log_to_stderr;
}

std::vector<std::string> GetLoggingDirectories() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_logging_directories;
}

bool VlogIsOn(int verbose_level) {
  return verbose_level <= g_vlog_level.load(std::memory_order_relaxed);
}

void SetVlogLevel(int verbose_level) {
  g_vlog_level.store(verbose_level > 0 ? verbose_level : 0,
                     std::memory_order_relaxed);
}

void LogMessageToSink(LogSeverity severity,
                      const char* file,
                      int line,
                      const std::string& message) {
  std::shared_ptr<spdlog::logger> logger;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    logger = g_logger;
  }

  const char* source_file = file != nullptr ? file : "";
  if (logger) {
    try {
      logger->log(spdlog::source_loc{source_file, line, ""},
                  ToSpdlogLevel(severity), message);
      if (severity == LogSeverity::Fatal)
        logger->flush();
    } catch (const std::exception& ex) {
      std::fprintf(stderr, "Rime: logging failed: %s\n", ex.what());
    } catch (...) {
      std::fprintf(stderr, "Rime: logging failed.\n");
    }
  } else {
    std::fprintf(stderr, "%c %s:%d] %s\n", LevelChar(severity), source_file,
                 line, message.c_str());
    std::fflush(stderr);
  }

  if (severity == LogSeverity::Fatal)
    std::abort();
}

}  // namespace rime

#endif  // RIME_ENABLE_LOGGING
