#pragma once

// Log su file SKSE + piccolo registro in memoria mostrato nel menu F1.

#include <deque>
#include <format>
#include <mutex>
#include <string>
#include <vector>

namespace bf
{
	namespace detail
	{
		inline std::mutex&              LogMx() { static std::mutex m; return m; }
		inline std::deque<std::string>& LogRing() { static std::deque<std::string> r; return r; }
	}

	inline void LogPush(const std::string& a_line)
	{
		std::lock_guard l(detail::LogMx());
		auto&           r = detail::LogRing();
		r.push_back(a_line);
		while (r.size() > 60) {
			r.pop_front();
		}
	}

	inline std::vector<std::string> LogSnapshot()
	{
		std::lock_guard l(detail::LogMx());
		return { detail::LogRing().begin(), detail::LogRing().end() };
	}

	inline void LogClear()
	{
		std::lock_guard l(detail::LogMx());
		detail::LogRing().clear();
	}

	template <class... Args>
	void Say(std::format_string<Args...> a_fmt, Args&&... a_args)
	{
		auto s = std::format(a_fmt, std::forward<Args>(a_args)...);
		SKSE::log::info("{}", s);
		LogPush(s);
	}
}
