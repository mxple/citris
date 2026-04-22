#pragma once
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/pattern_formatter.h>
#if defined(__linux__) && !defined(NDEBUG)
#  include <spdlog/sinks/basic_file_sink.h>
#endif

class Log {
    inline static std::shared_ptr<spdlog::logger> citris_;
#if defined(__linux__) && !defined(NDEBUG)
    inline static std::shared_ptr<spdlog::logger> tbp_;
#endif
    inline static std::chrono::steady_clock::time_point start_;

    struct ElapsedFlag : spdlog::custom_flag_formatter {
        void format(const spdlog::details::log_msg&, const std::tm&, spdlog::memory_buf_t& dest) override {
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start_).count();
            auto s = fmt::format("{:02d}:{:02d}:{:03d}", ms / 60000, (ms / 1000) % 60, ms % 1000);
            dest.append(s.data(), s.data() + s.size());
        }
        std::unique_ptr<custom_flag_formatter> clone() const override {
            return std::make_unique<ElapsedFlag>();
        }
    };

    static std::unique_ptr<spdlog::pattern_formatter> make_formatter(bool colors = true) {
        auto f = std::make_unique<spdlog::pattern_formatter>();
        f->add_flag<ElapsedFlag>('*');
        f->set_pattern(colors ? "%^[%*s] [%l] %n: %v%$" : "[%*s] [%l] %n: %v");
        return f;
    }

public:
    static void init() {
        start_ = std::chrono::steady_clock::now();

        citris_ = spdlog::stderr_color_mt("CITRIS");
        citris_->set_level(spdlog::level::trace);
        citris_->set_formatter(make_formatter());

#if defined(__linux__) && !defined(NDEBUG)
        tbp_ = spdlog::basic_logger_mt("TBP", "tbp_trace.log", /*truncate=*/true);
        tbp_->set_formatter(make_formatter(/*colors=*/false));
        tbp_->set_level(spdlog::level::trace);
#endif
    }

    static spdlog::logger& citris() { return *citris_; }
#if defined(__linux__) && !defined(NDEBUG)
    static spdlog::logger& tbp()    { return *tbp_; }
#endif
};

#define LOG_TRACE(...) ::Log::citris().trace(__VA_ARGS__)
#define LOG_DEBUG(...) ::Log::citris().debug(__VA_ARGS__)
#define LOG_INFO(...)  ::Log::citris().info(__VA_ARGS__)
#define LOG_WARN(...)  ::Log::citris().warn(__VA_ARGS__)
#define LOG_ERROR(...) ::Log::citris().error(__VA_ARGS__)

#if defined(__linux__) && !defined(NDEBUG)
#define TBP_TRACE(...) ::Log::tbp().trace(__VA_ARGS__)
#define TBP_DEBUG(...) ::Log::tbp().debug(__VA_ARGS__)
#define TBP_INFO(...)  ::Log::tbp().info(__VA_ARGS__)
#define TBP_WARN(...)  ::Log::tbp().warn(__VA_ARGS__)
#define TBP_ERROR(...) ::Log::tbp().error(__VA_ARGS__)
#else 
#define TBP_TRACE(...)
#define TBP_DEBUG(...)
#define TBP_INFO(...)
#define TBP_WARN(...)
#define TBP_ERROR(...)
#endif
