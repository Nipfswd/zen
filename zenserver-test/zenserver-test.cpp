// Copyright Noah Games, Inc. All Rights Reserved.

#define _SILENCE_CXX17_C_HEADER_DEPRECATION_WARNING

#include <zencore/compactbinary.h>
#include <zencore/compactbinarybuilder.h>
#include <zencore/compactbinarypackage.h>
#include <zencore/except.h>
#include <zencore/filesystem.h>
#include <zencore/iohash.h>
#include <zencore/string.h>
#include <zencore/thread.h>
#include <zencore/timer.h>
#include <zencore/trace.h>
#include <zenserverprocess.h>

#include <mimalloc.h>

#include <http_parser.h>

#if ZEN_PLATFORM_WINDOWS
#	pragma comment(lib, "Crypt32.lib")
#	pragma comment(lib, "Wldap32.lib")
#endif
#include <cpr/cpr.h>

#include <spdlog/spdlog.h>

#include <ppl.h>
#include <atomic>
#include <filesystem>
#include <map>
#include <random>

#include <atlbase.h>
#include <process.h>

#include <asio.hpp>

//////////////////////////////////////////////////////////////////////////

#include "projectclient.h"

//////////////////////////////////////////////////////////////////////////

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>
#undef DOCTEST_CONFIG_IMPLEMENT

using namespace fmt::literals;

/*

___  ___  _________  _________  ________        ________  ___       ___  _______   ________   _________
|\  \|\  \|\___   ___\\___   ___\\   __  \      |\   ____\|\  \     |\  \|\  ___ \ |\   ___  \|\___   ___\
\ \  \\\  \|___ \  \_\|___ \  \_\ \  \|\  \     \ \  \___|\ \  \    \ \  \ \   __/|\ \  \\ \  \|___ \  \_|
 \ \   __  \   \ \  \     \ \  \ \ \   ____\     \ \  \    \ \  \    \ \  \ \  \_|/_\ \  \\ \  \   \ \  \
  \ \  \ \  \   \ \  \     \ \  \ \ \  \___|      \ \  \____\ \  \____\ \  \ \  \_|\ \ \  \\ \  \   \ \  \
   \ \__\ \__\   \ \__\     \ \__\ \ \__\          \ \_______\ \_______\ \__\ \_______\ \__\\ \__\   \ \__\
	\|__|\|__|    \|__|      \|__|  \|__|           \|_______|\|_______|\|__|\|_______|\|__| \|__|    \|__|

*/

class HttpConnectionPool;

/**
 * Http client connection
 *
 * Represents an established socket connection to a certain endpoint
 */

class HttpClientConnection
{
	static HttpClientConnection* This(http_parser* Parser) { return (HttpClientConnection*)Parser->data; };

public:
	HttpClientConnection(asio::io_context& IoContext, HttpConnectionPool& Pool, asio::ip::tcp::socket&& InSocket)
	: m_IoContext(IoContext)
	, m_Pool(Pool)
	, m_Resolver(IoContext)
	, m_Socket(std::move(InSocket))
	{
	}
	~HttpClientConnection() {}

	HttpConnectionPool& ConnectionPool() { return m_Pool; }
	void				SetKeepAlive(bool NewState) { m_KeepAlive = NewState; }

	void Get(const std::string_view Server, int Port, const std::string_view Path)
	{
		http_parser_init(&m_HttpParser, HTTP_RESPONSE);
		m_HttpParser.data = this;

		m_HttpParserSettings = http_parser_settings{
			.on_message_begin	 = [](http_parser* p) -> int { return This(p)->OnMessageBegin(); },
			.on_url				 = nullptr,
			.on_status			 = nullptr,
			.on_header_field	 = [](http_parser* p, const char* data, size_t size) { return This(p)->OnHeader(data, size); },
			.on_header_value	 = [](http_parser* p, const char* data, size_t size) { return This(p)->OnHeaderValue(data, size); },
			.on_headers_complete = [](http_parser* p) -> int { return This(p)->OnHeadersComplete(); },
			.on_body			 = [](http_parser* p, const char* data, size_t size) { return This(p)->OnBody(data, size); },
			.on_message_complete = [](http_parser* p) -> int { return This(p)->OnMessageComplete(); },
			.on_chunk_header	 = nullptr,
			.on_chunk_complete	 = nullptr};

		m_Headers.reserve(16);

		zen::ExtendableStringBuilder<256> RequestBody;
		RequestBody << "GET " << Path << " HTTP/1.1\r\n";
		RequestBody << "Host: " << Server << "\r\n";
		RequestBody << "Accept: */*\r\n";
		RequestBody << "Connection: " << (m_KeepAlive ? "keep-alive" : "close") << "\r\n\r\n";	// TODO: support keep-alive

		m_RequestBody = RequestBody;

		OnConnected();
	}

private:
	void Reset() {}

	void OnError(const std::error_code& Error) { spdlog::error("HTTP client error! '{}'", Error.message()); }

	int OnHeader(const char* Data, size_t Bytes)
	{
		m_CurrentHeaderName = std::string_view(Data, Bytes);
		return 0;
	}
	int OnHeaderValue(const char* Data, size_t Bytes)
	{
		m_Headers.emplace_back(HeaderEntry{m_CurrentHeaderName, {Data, Bytes}});
		return 0;
	}
	int OnHeadersComplete()
	{
		spdlog::debug("Headers complete");
		return 0;
	}
	int OnMessageComplete()
	{
		if (http_should_keep_alive(&m_HttpParser))
		{
			Reset();
		}
		else
		{
			m_Socket.close();
			m_RequestState = RequestState::Done;
		}
		return 0;
	}
	int OnMessageBegin() { return 0; }
	int OnBody(const char* Data, size_t Bytes) { return 0; }

	void OnConnected()
	{
		// Send initial request payload
		asio::async_write(m_Socket,
						  asio::const_buffer(m_RequestBody.data(), m_RequestBody.size()),
						  [this](const std::error_code& Error, size_t Bytes) {
							  if (Error)
							  {
								  return OnError(Error);
							  }

							  OnRequestWritten();
						  });
	}

	void OnRequestWritten()
	{
		asio::async_read(m_Socket, m_ResponseBuffer, asio::transfer_at_least(1), [this](const std::error_code& Error, size_t Bytes) {
			if (Error)
			{
				return OnError(Error);
			}

			OnStatusLineRead(Bytes);
		});
	}

	void OnStatusLineRead(size_t Bytes)
	{
		// Parse

		size_t rv = http_parser_execute(&m_HttpParser, &m_HttpParserSettings, (const char*)m_ResponseBuffer.data(), Bytes);

		if (m_HttpParser.http_errno != 0)
		{
			// Something bad!

			spdlog::error("parse error {}", (uint32_t)m_HttpParser.http_errno);
		}

		switch (m_RequestState)
		{
			case RequestState::Init:
				asio::async_read(m_Socket,
								 m_ResponseBuffer,
								 asio::transfer_at_least(1),
								 [this](const std::error_code& Error, size_t Bytes) {
									 if (Error)
									 {
										 return OnError(Error);
									 }
									 OnStatusLineRead(Bytes);
								 });
				return;
			case RequestState::Done:
				break;
		}
	}

private:
	asio::io_context&		m_IoContext;
	HttpConnectionPool&		m_Pool;
	asio::ip::tcp::resolver m_Resolver;
	asio::ip::tcp::socket	m_Socket;
	std::string				m_Uri;
	std::string				m_RequestBody;	// Initial request data
	http_parser				m_HttpParser{};
	http_parser_settings	m_HttpParserSettings{};
	uint8_t					m_ResponseIoBuffer[4096];
	asio::mutable_buffer	m_ResponseBuffer{m_ResponseIoBuffer, sizeof m_ResponseIoBuffer};

	enum class RequestState
	{
		Init,
		Done
	};

	RequestState m_RequestState = RequestState::Init;

	struct HeaderEntry
	{
		std::string_view Name;
		std::string_view Value;
	};

	std::string_view		 m_CurrentHeaderName;  // Used while parsing headers
	std::vector<HeaderEntry> m_Headers;
	bool					 m_KeepAlive = false;
};

//////////////////////////////////////////////////////////////////////////

class HttpConnectionPool
{
public:
	HttpConnectionPool(asio::io_context& Context, std::string_view HostName, uint16_t Port);
	~HttpConnectionPool();

	std::unique_ptr<HttpClientConnection> GetConnection();
	void								  ReturnConnection(std::unique_ptr<HttpClientConnection>&& Connection);

private:
	zen::RwLock						   m_Lock;
	asio::io_context&				   m_Context;
	std::vector<HttpClientConnection*> m_AvailableConnections;
	std::string						   m_HostName;
	uint16_t						   m_Port;
};

HttpConnectionPool::HttpConnectionPool(asio::io_context& Context, std::string_view HostName, uint16_t Port)
: m_Context(Context)
, m_HostName(HostName)
, m_Port(Port)
{
}

HttpConnectionPool::~HttpConnectionPool()
{
	zen::RwLock::ExclusiveLockScope ScopedLock(m_Lock);

	for (auto $ : m_AvailableConnections)
	{
		delete $;
	}
}

std::unique_ptr<HttpClientConnection>
HttpConnectionPool::GetConnection()
{
	zen::RwLock::ExclusiveLockScope ScopedLock(m_Lock);

	if (m_AvailableConnections.empty())
	{
		zen::StringBuilder<16> Service;
		Service << int64_t(m_Port);

		asio::ip::tcp::resolver Resolver{m_Context};

		std::error_code ErrCode;
		auto			it	  = Resolver.resolve(m_HostName, Service, ErrCode);
		auto			itEnd = asio::ip::tcp::resolver::iterator();

		if (ErrCode)
		{
			return nullptr;
		}

		asio::ip::tcp::socket Socket{m_Context};
		asio::connect(Socket, it, ErrCode);

		if (ErrCode)
		{
			return nullptr;
		}

		return std::make_unique<HttpClientConnection>(m_Context, *this, std::move(Socket));
	}

	std::unique_ptr<HttpClientConnection> Connection{m_AvailableConnections.back()};
	m_AvailableConnections.pop_back();
	return std::move(Connection);
}

void
HttpConnectionPool::ReturnConnection(std::unique_ptr<HttpClientConnection>&& Connection)
{
	zen::RwLock::ExclusiveLockScope ScopedLock(m_Lock);
	m_AvailableConnections.emplace_back(Connection.release());
}

//////////////////////////////////////////////////////////////////////////

class HttpContext
{
public:
	HttpContext(asio::io_context& Context) : m_Context(Context) {}
	~HttpContext() = default;

	std::unique_ptr<HttpClientConnection> GetConnection(std::string_view HostName, uint16_t Port)
	{
		return ConnectionPool(HostName, Port).GetConnection();
	}

	void ReturnConnection(std::unique_ptr<HttpClientConnection> Connection)
	{
		Connection->ConnectionPool().ReturnConnection(std::move(Connection));
	}

	HttpConnectionPool& ConnectionPool(std::string_view HostName, uint16_t Port)
	{
		zen::RwLock::ExclusiveLockScope _(m_Lock);
		ConnectionId					ConnId{std::string(HostName), Port};

		if (auto It = m_ConnectionPools.find(ConnId); It == end(m_ConnectionPools))
		{
			// Not found - create new entry

			auto In = m_ConnectionPools.insert({ConnId, std::move(HttpConnectionPool(m_Context, HostName, Port))});

			return In.first->second;
		}
		else
		{
			return It->second;
		}
	}

private:
	asio::io_context& m_Context;

	struct ConnectionId
	{
		inline bool operator<(const ConnectionId& Rhs) const
		{
			if (HostName != Rhs.HostName)
			{
				return HostName < Rhs.HostName;
			}

			return Port < Rhs.Port;
		}

		std::string HostName;
		uint16_t	Port;
	};

	zen::RwLock								   m_Lock;
	std::map<ConnectionId, HttpConnectionPool> m_ConnectionPools;
};

//////////////////////////////////////////////////////////////////////////

class HttpClientRequest
{
public:
	HttpClientRequest(HttpContext& Context) : m_HttpContext(Context) {}
	~HttpClientRequest()
	{
		if (m_Connection)
		{
			m_HttpContext.ReturnConnection(std::move(m_Connection));
		}
	}

	void Get(const std::string_view Url)
	{
		http_parser_url ParsedUrl;
		int				ErrCode = http_parser_parse_url(Url.data(), Url.size(), 0, &ParsedUrl);

		if (ErrCode)
		{
			ZEN_NOT_IMPLEMENTED();
		}

		if ((ParsedUrl.field_set & (UF_HOST | UF_PORT | UF_PATH)) != (UF_HOST | UF_PORT | UF_PATH))
		{
			// Bad URL
		}

		std::string_view HostName(Url.data() + ParsedUrl.field_data[UF_HOST].off, ParsedUrl.field_data[UF_HOST].len);
		std::string_view Path(Url.data() + ParsedUrl.field_data[UF_PATH].off);

		m_Connection = m_HttpContext.GetConnection(HostName, ParsedUrl.port);
		m_Connection->Get(HostName, ParsedUrl.port, Path);
	}

private:
	HttpContext&						  m_HttpContext;
	std::unique_ptr<HttpClientConnection> m_Connection;
};

//////////////////////////////////////////////////////////////////////////
//
// Custom logging -- test code, this should be tweaked
//

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

//////////////////////////////////////////////////////////////////////////

#if 0
#	include <cpr/cpr.h>

#	pragma comment(lib, "Crypt32.lib")
#	pragma comment(lib, "Wldap32.lib")

int
main()
{
	mi_version();

	zen::Sleep(1000);

	zen::Stopwatch timer;

	const int RequestCount = 100000;

	cpr::Session Sessions[10];

	for (auto& Session : Sessions)
	{
		Session.SetUrl(cpr::Url{"http://localhost:1337/test/hello"});
		//Session.SetUrl(cpr::Url{ "http://arn-wd-l0182:1337/test/hello" });
	}

	auto Run = [](cpr::Session& Session) {
		for (int i = 0; i < 10000; ++i)
		{
			cpr::Response Result = Session.Get();
			
			if (Result.status_code != 200)
			{
				spdlog::warn("request response: {}", Result.status_code);
			}
		}
	};

	Concurrency::parallel_invoke([&] { Run(Sessions[0]); },
								 [&] { Run(Sessions[1]); },
								 [&] { Run(Sessions[2]); },
								 [&] { Run(Sessions[3]); },
								 [&] { Run(Sessions[4]); },
								 [&] { Run(Sessions[5]); },
								 [&] { Run(Sessions[6]); },
								 [&] { Run(Sessions[7]); },
								 [&] { Run(Sessions[8]); },
								 [&] { Run(Sessions[9]); });

	// cpr::Response r = cpr::Get(cpr::Url{ "http://localhost:1337/test/hello" });

	spdlog::info("{} requests in {} ({})",
				 RequestCount,
				 zen::NiceTimeSpanMs(timer.getElapsedTimeMs()),
				 zen::NiceRate(RequestCount, (uint32_t)timer.getElapsedTimeMs(), "req"));

	return 0;
}
#elif 0
//#include <restinio/all.hpp>

int
main()
{
	mi_version();
	restinio::run(restinio::on_thread_pool(32).port(8080).request_handler(
		[](auto req) { return req->create_response().set_body("Hello, World!").done(); }));
	return 0;
}
#else

ZenTestEnvironment TestEnv;

int
main(int argc, char** argv)
{
	mi_version();

	zencore_forcelinktests();

	spdlog::set_level(spdlog::level::debug);
	spdlog::set_formatter(std::make_unique<logging::full_formatter>("test", std::chrono::system_clock::now()));

	std::filesystem::path ProgramBaseDir = std::filesystem::path(argv[0]).parent_path();
	std::filesystem::path TestBaseDir	 = ProgramBaseDir.parent_path().parent_path() / ".test";

	TestEnv.Initialize(ProgramBaseDir, TestBaseDir);

	spdlog::info("Running tests...");
	return doctest::Context(argc, argv).run();
}

#	if 1
TEST_CASE("asio.http")
{
	std::filesystem::path TestDir = TestEnv.CreateNewTestDir();

	ZenServerInstance Instance(TestEnv);
	Instance.SetTestDir(TestDir);
	Instance.SpawnServer(13337);

	spdlog::info("Waiting...");

	Instance.WaitUntilReady();

	// asio test

	asio::io_context  IoContext;
	HttpContext		  HttpCtx(IoContext);
	HttpClientRequest Request(HttpCtx);
	Request.Get("http://localhost:13337/test/hello");

	IoContext.run();
}
#	endif

TEST_CASE("default.single")
{
	std::filesystem::path TestDir = TestEnv.CreateNewTestDir();

	ZenServerInstance Instance(TestEnv);
	Instance.SetTestDir(TestDir);
	Instance.SpawnServer(13337);

	spdlog::info("Waiting...");

	Instance.WaitUntilReady();

	std::atomic<uint64_t> RequestCount{0};
	std::atomic<uint64_t> BatchCounter{0};

	spdlog::info("Running single server test...");

	auto IssueTestRequests = [&] {
		const uint64_t BatchNo	= BatchCounter.fetch_add(1);
		const DWORD	   ThreadId = GetCurrentThreadId();

		spdlog::info("query batch {} started (thread {})", BatchNo, ThreadId);
		cpr::Session cli;
		cli.SetUrl(cpr::Url{"http://localhost:13337/test/hello"});

		for (int i = 0; i < 10000; ++i)
		{
			auto res = cli.Get();
			++RequestCount;
		}
		spdlog::info("query batch {} ended (thread {})", BatchNo, ThreadId);
	};

	auto fun10 = [&] {
		Concurrency::parallel_invoke(IssueTestRequests,
									 IssueTestRequests,
									 IssueTestRequests,
									 IssueTestRequests,
									 IssueTestRequests,
									 IssueTestRequests,
									 IssueTestRequests,
									 IssueTestRequests,
									 IssueTestRequests,
									 IssueTestRequests);
	};

	zen::Stopwatch timer;

	// Concurrency::parallel_invoke(fun10, fun10, fun, fun, fun, fun, fun, fun, fun, fun);
	Concurrency::parallel_invoke(IssueTestRequests,
								 IssueTestRequests,
								 IssueTestRequests,
								 IssueTestRequests,
								 IssueTestRequests,
								 IssueTestRequests,
								 IssueTestRequests,
								 IssueTestRequests,
								 IssueTestRequests,
								 IssueTestRequests);

	uint64_t Elapsed = timer.getElapsedTimeMs();

	spdlog::info("{} requests in {} ({})",
				 RequestCount,
				 zen::NiceTimeSpanMs(Elapsed),
				 zen::NiceRate(RequestCount, (uint32_t)Elapsed, "req"));
}

TEST_CASE("multi.basic")
{
	ZenServerInstance	  Instance1(TestEnv);
	std::filesystem::path TestDir1 = TestEnv.CreateNewTestDir();
	Instance1.SetTestDir(TestDir1);
	Instance1.SpawnServer(13337);

	ZenServerInstance	  Instance2(TestEnv);
	std::filesystem::path TestDir2 = TestEnv.CreateNewTestDir();
	Instance2.SetTestDir(TestDir2);
	Instance2.SpawnServer(13338);

	spdlog::info("Waiting...");

	Instance1.WaitUntilReady();
	Instance2.WaitUntilReady();

	std::atomic<uint64_t> RequestCount{0};
	std::atomic<uint64_t> BatchCounter{0};

	auto IssueTestRequests = [&](int PortNumber) {
		const uint64_t BatchNo	= BatchCounter.fetch_add(1);
		const DWORD	   ThreadId = GetCurrentThreadId();

		spdlog::info("query batch {} started (thread {}) for port {}", BatchNo, ThreadId, PortNumber);

		cpr::Session cli;
		cli.SetUrl(cpr::Url{"http://localhost:{}/test/hello"_format(PortNumber)});

		for (int i = 0; i < 10000; ++i)
		{
			auto res = cli.Get();
			++RequestCount;
		}
		spdlog::info("query batch {} ended (thread {})", BatchNo, ThreadId);
	};

	zen::Stopwatch timer;

	spdlog::info("Running multi-server test...");

	Concurrency::parallel_invoke([&] { IssueTestRequests(13337); },
								 [&] { IssueTestRequests(13338); },
								 [&] { IssueTestRequests(13337); },
								 [&] { IssueTestRequests(13338); });

	uint64_t Elapsed = timer.getElapsedTimeMs();

	spdlog::info("{} requests in {} ({})",
				 RequestCount,
				 zen::NiceTimeSpanMs(Elapsed),
				 zen::NiceRate(RequestCount, (uint32_t)Elapsed, "req"));
}

TEST_CASE("cas.basic")
{
	std::filesystem::path TestDir = TestEnv.CreateNewTestDir();

	const uint16_t PortNumber = 13337;

	const int IterationCount = 1000;

	std::vector<int>		 ChunkSizes(IterationCount);
	std::vector<zen::IoHash> ChunkHashes(IterationCount);

	{
		ZenServerInstance Instance1(TestEnv);
		Instance1.SetTestDir(TestDir);
		Instance1.SpawnServer(PortNumber);
		Instance1.WaitUntilReady();

		std::atomic<uint64_t> RequestCount{0};
		std::atomic<uint64_t> BatchCounter{0};

		zen::Stopwatch timer;

		std::mt19937_64 mt;

		auto BaseUri = "http://localhost:{}/cas"_format(PortNumber);

		cpr::Session cli;
		cli.SetUrl(cpr::Url{BaseUri});

		// Populate CAS with some generated data

		for (int i = 0; i < IterationCount; ++i)
		{
			const int	ChunkSize = mt() % 10000 + 5;
			std::string body	  = fmt::format("{}", i);
			body.resize(ChunkSize, ' ');

			ChunkSizes[i]  = ChunkSize;
			ChunkHashes[i] = zen::IoHash::HashMemory(body.data(), body.size());

			cli.SetBody(body);

			auto res = cli.Post();
			CHECK(!res.error);

			++RequestCount;
		}

		// Verify that the chunks persisted

		for (int i = 0; i < IterationCount; ++i)
		{
			zen::ExtendableStringBuilder<128> Uri;
			Uri << BaseUri << "/";
			ChunkHashes[i].ToHexString(Uri);

			auto res = cpr::Get(cpr::Url{Uri.c_str()});
			CHECK(!res.error);
			CHECK(res.status_code == 200);
			CHECK(res.text.size() == ChunkSizes[i]);

			zen::IoHash Hash = zen::IoHash::HashMemory(res.text.data(), res.text.size());

			CHECK(ChunkHashes[i] == Hash);

			++RequestCount;
		}

		uint64_t Elapsed = timer.getElapsedTimeMs();

		spdlog::info("{} requests in {} ({})",
					 RequestCount,
					 zen::NiceTimeSpanMs(Elapsed),
					 zen::NiceRate(RequestCount, (uint32_t)Elapsed, "req"));
	}

	// Verify that the data persists between process runs (the previous server has exited at this point)

	{
		ZenServerInstance Instance2(TestEnv);
		Instance2.SetTestDir(TestDir);
		Instance2.SpawnServer(PortNumber);
		Instance2.WaitUntilReady();

		for (int i = 0; i < IterationCount; ++i)
		{
			zen::ExtendableStringBuilder<128> Uri;
			Uri << "http://localhost:{}/cas/"_format(PortNumber);
			ChunkHashes[i].ToHexString(Uri);

			auto res = cpr::Get(cpr::Url{Uri.c_str()});
			CHECK(res.status_code == 200);
			CHECK(res.text.size() == ChunkSizes[i]);

			zen::IoHash Hash = zen::IoHash::HashMemory(res.text.data(), res.text.size());

			CHECK(ChunkHashes[i] == Hash);
		}
	}
}

TEST_CASE("project.basic")
{
	using namespace std::literals;

	std::filesystem::path TestDir = TestEnv.CreateNewTestDir();

	const uint16_t PortNumber = 13337;

	ZenServerInstance Instance1(TestEnv);
	Instance1.SetTestDir(TestDir);
	Instance1.SpawnServer(PortNumber);
	Instance1.WaitUntilReady();

	std::atomic<uint64_t> RequestCount{0};

	zen::Stopwatch timer;

	std::mt19937_64 mt;

	zen::StringBuilder<64> BaseUri;
	BaseUri << "http://localhost:{}/prj/test"_format(PortNumber);

	SUBCASE("build store init")
	{
		{
			{
				zen::CbObjectWriter Body;
				Body << "id"
					 << "test";
				Body << "root"
					 << "/zooom";
				Body << "project"
					 << "/zooom";
				Body << "engine"
					 << "/zooom";

				zen::MemoryOutStream MemOut;
				zen::BinaryWriter	 Writer{MemOut};
				Body.Save(Writer);

				auto Response = cpr::Post(cpr::Url{BaseUri.c_str()}, cpr::Body{(const char*)MemOut.Data(), MemOut.Size()});
				CHECK(Response.status_code == 201);
			}

			{
				auto Response = cpr::Get(cpr::Url{BaseUri.c_str()});
				CHECK(Response.status_code == 200);

				zen::CbObjectView ResponseObject = zen::CbFieldView(Response.text.data()).AsObjectView();

				CHECK(ResponseObject["id"].AsString() == "test"sv);
				CHECK(ResponseObject["root"].AsString() == "/zooom"sv);
			}
		}

		BaseUri << "/oplog/ps5";

		{
			{
				zen::StringBuilder<64> PostUri;
				PostUri << BaseUri;
				auto Response = cpr::Post(cpr::Url{PostUri.c_str()});
				CHECK(Response.status_code == 201);
			}

			{
				auto Response = cpr::Get(cpr::Url{BaseUri.c_str()});
				CHECK(Response.status_code == 200);

				zen::CbObjectView ResponseObject = zen::CbFieldView(Response.text.data()).AsObjectView();

				CHECK(ResponseObject["id"].AsString() == "ps5"sv);
				CHECK(ResponseObject["project"].AsString() == "test"sv);
			}
		}

		SUBCASE("build store persistence")
		{
			uint8_t AttachData[] = {1, 2, 3};

			zen::CbAttachment Attach{zen::SharedBuffer::Clone(zen::MemoryView{AttachData, 3})};

			zen::CbObjectWriter OpWriter;
			OpWriter << "key"
					 << "foo"
					 << "attachment" << Attach;

			const std::string_view ChunkId{
				"00000000"
				"00000000"
				"00010000"};
			auto FileOid = zen::Oid::FromHexString(ChunkId);

			OpWriter.BeginArray("files");
			OpWriter.BeginObject();
			OpWriter << "id" << FileOid;
			OpWriter << "path" << __FILE__;
			OpWriter.EndObject();
			OpWriter.EndArray();

			OpWriter.BeginArray("serverfiles");
			OpWriter.BeginObject();
			OpWriter << "id" << FileOid;
			OpWriter << "path" << __FILE__;
			OpWriter.EndObject();
			OpWriter.EndArray();

			zen::CbObject Op = OpWriter.Save();

			zen::MemoryOutStream MemOut;
			zen::BinaryWriter	 Writer(MemOut);
			zen::CbPackage		 OpPackage(Op);
			OpPackage.AddAttachment(Attach);
			OpPackage.Save(Writer);

			{
				zen::StringBuilder<64> PostUri;
				PostUri << BaseUri << "/new";
				auto Response = cpr::Post(cpr::Url{PostUri.c_str()}, cpr::Body{(const char*)MemOut.Data(), MemOut.Size()});

				REQUIRE(!Response.error);
				CHECK(Response.status_code == 201);
			}

			// Read file data

			{
				zen::StringBuilder<128> ChunkGetUri;
				ChunkGetUri << BaseUri << "/" << ChunkId;
				auto Response = cpr::Get(cpr::Url{ChunkGetUri.c_str()});

				REQUIRE(!Response.error);
				CHECK(Response.status_code == 200);
			}

			{
				zen::StringBuilder<128> ChunkGetUri;
				ChunkGetUri << BaseUri << "/" << ChunkId << "?offset=1&size=10";
				auto Response = cpr::Get(cpr::Url{ChunkGetUri.c_str()});

				REQUIRE(!Response.error);
				CHECK(Response.status_code == 200);
				CHECK(Response.text.size() == 10);
			}

			spdlog::info("+++++++");
		}
		SUBCASE("build store op commit") { spdlog::info("-------"); }
	}

	const uint64_t Elapsed = timer.getElapsedTimeMs();

	spdlog::info("{} requests in {} ({})",
				 RequestCount,
				 zen::NiceTimeSpanMs(Elapsed),
				 zen::NiceRate(RequestCount, (uint32_t)Elapsed, "req"));
}

TEST_CASE("project.pipe")
{
	using namespace std::literals;

	std::filesystem::path TestDir = TestEnv.CreateNewTestDir();

	const uint16_t PortNumber = 13337;

	ZenServerInstance Instance1(TestEnv);
	Instance1.SetTestDir(TestDir);
	Instance1.SpawnServer(PortNumber);
	Instance1.WaitUntilReady();

	zen::LocalProjectClient LocalClient(PortNumber);

	zen::CbObjectWriter Cbow;
	Cbow << "hey" << 42;

	zen::CbObject Response = LocalClient.MessageTransaction(Cbow.Save());
}

#endif

