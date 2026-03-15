// Copyright Noah Games, Inc. All Rights Reserved.

#include "logging.h"

#include "config.h"

#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/ansicolor_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <memory>

// Custom logging -- test code, this should be tweaked

namespace logging {

using namespace spdlog;
using namespace spdlog::details;
using namespace std::literals;

class full_formatter final : public spdlog::formatter
{
public:
	full_formatter(std::string_view LogId, std::chrono::time_point<std::chrono::system_clock> Epoch) : m_Epoch(Epoch), m_LogId(LogId) {}

	virtual std::unique_ptr<formatter> clone() const override { return std::make_unique<full_formatter>(m_LogId, m_Epoch); }

	static constexpr bool UseDate = false;

	virtual void format(const details::log_msg& msg, memory_buf_t& dest) override
	{
		using std::chrono::duration_cast;
		using std::chrono::milliseconds;
		using std::chrono::seconds;

		if constexpr (UseDate)
		{
			auto secs = std::chrono::duration_cast<seconds>(msg.time.time_since_epoch());
			if (secs != m_LastLogSecs)
			{
				m_CachedTm	  = os::localtime(log_clock::to_time_t(msg.time));
				m_LastLogSecs = secs;
			}
		}

		const auto& tm_time = m_CachedTm;

		// cache the date/time part for the next second.
		auto duration = msg.time - m_Epoch;
		auto secs	  = duration_cast<seconds>(duration);

		if (m_CacheTimestamp != secs || m_CachedDatetime.size() == 0)
		{
			m_CachedDatetime.clear();
			m_CachedDatetime.push_back('[');

			if constexpr (UseDate)
			{
				fmt_helper::append_int(tm_time.tm_year + 1900, m_CachedDatetime);
				m_CachedDatetime.push_back('-');

				fmt_helper::pad2(tm_time.tm_mon + 1, m_CachedDatetime);
				m_CachedDatetime.push_back('-');

				fmt_helper::pad2(tm_time.tm_mday, m_CachedDatetime);
				m_CachedDatetime.push_back(' ');

				fmt_helper::pad2(tm_time.tm_hour, m_CachedDatetime);
				m_CachedDatetime.push_back(':');

				fmt_helper::pad2(tm_time.tm_min, m_CachedDatetime);
				m_CachedDatetime.push_back(':');

				fmt_helper::pad2(tm_time.tm_sec, m_CachedDatetime);
			}
			else
			{
				int Count = int(secs.count());

				const int LogSecs = Count % 60;
				Count /= 60;

				const int LogMins = Count % 60;
				Count /= 60;

				const int LogHours = Count;

				fmt_helper::pad2(LogHours, m_CachedDatetime);
				m_CachedDatetime.push_back(':');
				fmt_helper::pad2(LogMins, m_CachedDatetime);
				m_CachedDatetime.push_back(':');
				fmt_helper::pad2(LogSecs, m_CachedDatetime);
			}

			m_CachedDatetime.push_back('.');

			m_CacheTimestamp = secs;
		}

		dest.append(m_CachedDatetime.begin(), m_CachedDatetime.end());

		auto millis = fmt_helper::time_fraction<milliseconds>(msg.time);
		fmt_helper::pad3(static_cast<uint32_t>(millis.count()), dest);
		dest.push_back(']');
		dest.push_back(' ');

		if (!m_LogId.empty())
		{
			dest.push_back('[');
			fmt_helper::append_string_view(m_LogId, dest);
			dest.push_back(']');
			dest.push_back(' ');
		}

		// append logger name if exists
		if (msg.logger_name.size() > 0)
		{
			dest.push_back('[');
			fmt_helper::append_string_view(msg.logger_name, dest);
			dest.push_back(']');
			dest.push_back(' ');
		}

		dest.push_back('[');
		// wrap the level name with color
		msg.color_range_start = dest.size();
		fmt_helper::append_string_view(level::to_string_view(msg.level), dest);
		msg.color_range_end = dest.size();
		dest.push_back(']');
		dest.push_back(' ');

		// add source location if present
		if (!msg.source.empty())
		{
			dest.push_back('[');
			const char* filename = details::short_filename_formatter<details::null_scoped_padder>::basename(msg.source.filename);
			fmt_helper::append_string_view(filename, dest);
			dest.push_back(':');
			fmt_helper::append_int(msg.source.line, dest);
			dest.push_back(']');
			dest.push_back(' ');
		}

		fmt_helper::append_string_view(msg.payload, dest);
		fmt_helper::append_string_view("\n"sv, dest);
	}

private:
	std::chrono::time_point<std::chrono::system_clock> m_Epoch;
	std::tm											   m_CachedTm;
	std::chrono::seconds							   m_LastLogSecs;
	std::chrono::seconds							   m_CacheTimestamp{0};
	memory_buf_t									   m_CachedDatetime;
	std::string										   m_LogId;
};

}  // namespace logging

bool
EnableVTMode()
{
	// Set output mode to handle virtual terminal sequences
	HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hOut == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	DWORD dwMode = 0;
	if (!GetConsoleMode(hOut, &dwMode))
	{
		return false;
	}

	dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	if (!SetConsoleMode(hOut, dwMode))
	{
		return false;
	}

	return true;
}

void
InitializeLogging(const ZenServerOptions& GlobalOptions)
{
	EnableVTMode();

	auto& sinks = spdlog::default_logger()->sinks();
	sinks.clear();
	sinks.push_back(std::make_shared<spdlog::sinks::ansicolor_stdout_sink_mt>());
	spdlog::set_level(spdlog::level::debug);
	spdlog::set_formatter(std::make_unique<logging::full_formatter>(GlobalOptions.LogId, std::chrono::system_clock::now()));
}

spdlog::logger&
ConsoleLog()
{
	static auto ConLogger = spdlog::stdout_color_mt("console");

	ConLogger->set_pattern("%v");

	return *ConLogger;
}

