// Copyright Noah Games, Inc. All Rights Reserved.

#include <zencore/httpserver.h>

#define _WINSOCKAPI_
#include <zencore/windows.h>
#include "iothreadpool.h"

#include <atlbase.h>
#include <conio.h>
#include <http.h>
#include <new.h>
#include <zencore/compactbinary.h>
#include <zencore/iobuffer.h>
#include <zencore/refcount.h>
#include <zencore/string.h>
#include <zencore/thread.h>
#include <charconv>
#include <span>
#include <string_view>

#include <spdlog/spdlog.h>

#include <doctest/doctest.h>

#pragma comment(lib, "httpapi.lib")

//////////////////////////////////////////////////////////////////////////

std::wstring
UTF8_to_wstring(const char* in)
{
	std::wstring out;
	unsigned int codepoint;

	while (*in != 0)
	{
		unsigned char ch = static_cast<unsigned char>(*in);

		if (ch <= 0x7f)
			codepoint = ch;
		else if (ch <= 0xbf)
			codepoint = (codepoint << 6) | (ch & 0x3f);
		else if (ch <= 0xdf)
			codepoint = ch & 0x1f;
		else if (ch <= 0xef)
			codepoint = ch & 0x0f;
		else
			codepoint = ch & 0x07;

		++in;

		if (((*in & 0xc0) != 0x80) && (codepoint <= 0x10ffff))
		{
			if (sizeof(wchar_t) > 2)
			{
				out.append(1, static_cast<wchar_t>(codepoint));
			}
			else if (codepoint > 0xffff)
			{
				out.append(1, static_cast<wchar_t>(0xd800 + (codepoint >> 10)));
				out.append(1, static_cast<wchar_t>(0xdc00 + (codepoint & 0x03ff)));
			}
			else if (codepoint < 0xd800 || codepoint >= 0xe000)
			{
				out.append(1, static_cast<wchar_t>(codepoint));
			}
		}
	}

	return out;
}

//////////////////////////////////////////////////////////////////////////

const char*
ReasonStringForHttpResultCode(int HttpCode)
{
	switch (HttpCode)
	{
			// 1xx Informational

		case 100:
			return "Continue";
		case 101:
			return "Switching Protocols";

			// 2xx Success

		case 200:
			return "OK";
		case 201:
			return "Created";
		case 202:
			return "Accepted";
		case 204:
			return "No Content";
		case 205:
			return "Reset Content";
		case 206:
			return "Partial Content";

			// 3xx Redirection

		case 300:
			return "Multiple Choices";
		case 301:
			return "Moved Permanently";
		case 302:
			return "Found";
		case 303:
			return "See Other";
		case 304:
			return "Not Modified";
		case 305:
			return "Use Proxy";
		case 306:
			return "Switch Proxy";
		case 307:
			return "Temporary Redirect";
		case 308:
			return "Permanent Redirect";

			// 4xx Client errors

		case 400:
			return "Bad Request";
		case 401:
			return "Unauthorized";
		case 402:
			return "Payment Required";
		case 403:
			return "Forbidden";
		case 404:
			return "Not Found";
		case 405:
			return "Method Not Allowed";
		case 406:
			return "Not Acceptable";
		case 407:
			return "Proxy Authentication Required";
		case 408:
			return "Request Timeout";
		case 409:
			return "Conflict";
		case 410:
			return "Gone";
		case 411:
			return "Length Required";
		case 412:
			return "Precondition Failed";
		case 413:
			return "Payload Too Large";
		case 414:
			return "URI Too Long";
		case 415:
			return "Unsupported Media Type";
		case 416:
			return "Range Not Satisifiable";
		case 417:
			return "Expectation Failed";
		case 418:
			return "I'm a teapot";
		case 421:
			return "Misdirected Request";
		case 422:
			return "Unprocessable Entity";
		case 423:
			return "Locked";
		case 424:
			return "Failed Dependency";
		case 425:
			return "Too Early";
		case 426:
			return "Upgrade Required";
		case 428:
			return "Precondition Required";
		case 429:
			return "Too Many Requests";
		case 431:
			return "Request Header Fields Too Large";

			// 5xx Server errors

		case 500:
			return "Internal Server Error";
		case 501:
			return "Not Implemented";
		case 502:
			return "Bad Gateway";
		case 503:
			return "Service Unavailable";
		case 504:
			return "Gateway Timeout";
		case 505:
			return "HTTP Version Not Supported";
		case 506:
			return "Variant Also Negotiates";
		case 507:
			return "Insufficient Storage";
		case 508:
			return "Loop Detected";
		case 510:
			return "Not Extended";
		case 511:
			return "Network Authentication Required";

		default:
			return "Unknown Result";
	}
}

namespace zen {

//////////////////////////////////////////////////////////////////////////

HttpServerRequest::HttpServerRequest()
{
}

HttpServerRequest::~HttpServerRequest()
{
}

void
HttpServerRequest::WriteResponse(HttpResponse HttpResponseCode, CbObject Data)
{
#if 0
	struct Visitor : public ICbVisitor
	{
		virtual void SetName(std::string_view Name) override { OutText << '\'' << Name << "': "; }
		virtual void BeginObject() override { OutText << "{ "; }
		virtual void EndObject() override { OutText << "}"; }
		virtual void BeginArray() override { OutText << "[ "; }
		virtual void EndArray() override { OutText << " ]"; }

		virtual void VisitNull() override { OutText << "null"; }
		virtual void VisitBinary(SharedBuffer Value) override { ZEN_UNUSED(Value); }
		virtual void VisitString(std::string_view Value) override { ZEN_UNUSED(Value); }
		virtual void VisitInteger(int64_t Value) override { OutText << Value; }
		virtual void VisitInteger(uint64_t Value) override { OutText << Value; }
		virtual void VisitFloat(float Value) override { ZEN_UNUSED(Value); }
		virtual void VisitDouble(double Value) override { ZEN_UNUSED(Value); }
		virtual void VisitBool(bool Value) override { OutText << Value; }
		virtual void VisitCbAttachment(const IoHash& Value) override { ZEN_UNUSED(Value); }
		virtual void VisitBinaryAttachment(const IoHash& Value) override { ZEN_UNUSED(Value); }
		virtual void VisitHash(const IoHash& Value) override { ZEN_UNUSED(Value); }
		virtual void VisitUuid(const Guid& Value) override { ZEN_UNUSED(Value); }
		virtual void VisitObjectId(const Oid& Value) override { ZEN_UNUSED(Value); }
		virtual void VisitDateTime(DateTime Value) override { ZEN_UNUSED(Value); }
		virtual void VisitTimeSpan(TimeSpan Value) override { ZEN_UNUSED(Value); }

		ExtendableStringBuilder<256> OutText;
	} _;
	// Data.CreateRefIterator().VisitFields(_);
	return WriteResponse(HttpResponseCode, HttpContentType::kJSON, _.OutText);
#else
	SharedBuffer			Buf = Data.GetBuffer();
	std::array<IoBuffer, 1> buffers{IoBufferBuilder::MakeCloneFromMemory(Buf.GetData(), Buf.GetSize())};
	return WriteResponse(HttpResponseCode, HttpContentType::kCbObject, buffers);
#endif
}

void
HttpServerRequest::WriteResponse(HttpResponse HttpResponseCode, HttpContentType ContentType, std::string_view ResponseString)
{
	return WriteResponse(HttpResponseCode, ContentType, std::u8string_view{(char8_t*)ResponseString.data(), ResponseString.size()});
}

void
HttpServerRequest::WriteResponse(HttpResponse HttpResponseCode, HttpContentType ContentType, IoBuffer Blob)
{
	std::array<IoBuffer, 1> buffers{Blob};
	return WriteResponse(HttpResponseCode, ContentType, buffers);
}

HttpServerRequest::QueryParams
HttpServerRequest::GetQueryParams()
{
	QueryParams Params;

	const std::string_view QStr = QueryString();

	const char* QueryIt	 = QStr.data();
	const char* QueryEnd = QueryIt + QStr.size();

	while (QueryIt != QueryEnd)
	{
		if (*QueryIt == '&')
		{
			++QueryIt;
			continue;
		}

		const std::string_view Query{QueryIt, QueryEnd};

		size_t DelimIndex = Query.find('&', 0);

		if (DelimIndex == std::string_view::npos)
		{
			DelimIndex = Query.size();
		}

		std::string_view ThisQuery{QueryIt, DelimIndex};

		size_t EqIndex = ThisQuery.find('=', 0);

		if (EqIndex != std::string_view::npos)
		{
			std::string_view Parm{ThisQuery.data(), EqIndex};
			ThisQuery.remove_prefix(EqIndex + 1);

			Params.KvPairs.emplace_back(Parm, ThisQuery);
		}

		QueryIt += DelimIndex;
	}

	return std::move(Params);
}

//////////////////////////////////////////////////////////////////////////
//
// HTTP
//

class HttpSysServer;
class HttpTransaction;

class HttpSysRequestHandler
{
public:
	HttpSysRequestHandler(HttpTransaction& InRequest) : m_Request(InRequest) {}
	virtual ~HttpSysRequestHandler() = default;

	virtual void				   IssueRequest()														= 0;
	virtual HttpSysRequestHandler* HandleCompletion(ULONG IoResult, ULONG_PTR NumberOfBytesTransferred) = 0;

	HttpTransaction& Transaction() { return m_Request; }

private:
	HttpTransaction& m_Request;	 // Outermost HTTP transaction object
};

/** HTTP transaction

	There will be an instance of this per pending and in-flight HTTP transaction

  */
class HttpTransaction
{
public:
	HttpTransaction(HttpSysServer& Server) : m_HttpServer(Server), m_HttpHandler(&m_InitialHttpHandler) {}

	virtual ~HttpTransaction() {}

	enum class Status
	{
		kDone,
		kRequestPending
	};

	Status HandleCompletion(ULONG IoResult, ULONG_PTR NumberOfBytesTransferred);

	static void __stdcall IoCompletionCallback(PTP_CALLBACK_INSTANCE Instance,
											   PVOID				 pContext /* HttpSysServer */,
											   PVOID				 pOverlapped,
											   ULONG				 IoResult,
											   ULONG_PTR			 NumberOfBytesTransferred,
											   PTP_IO				 Io)
	{
		UNREFERENCED_PARAMETER(Io);
		UNREFERENCED_PARAMETER(Instance);
		UNREFERENCED_PARAMETER(pContext);

		// Note that for a given transaction we may be in this completion function on more
		// than one thread at any given moment. This means we need to be careful about what
		// happens in here

		HttpTransaction* Transaction = CONTAINING_RECORD(pOverlapped, HttpTransaction, m_HttpOverlapped);

		if (Transaction->HandleCompletion(IoResult, NumberOfBytesTransferred) == HttpTransaction::Status::kDone)
		{
			delete Transaction;
		}
	}

	void IssueInitialRequest();

	PTP_IO				  Iocp();
	HANDLE				  RequestQueueHandle();
	inline OVERLAPPED*	  Overlapped() { return &m_HttpOverlapped; }
	inline HttpSysServer& Server() { return m_HttpServer; }

	inline PHTTP_REQUEST HttpRequest() { return m_InitialHttpHandler.HttpRequest(); }

protected:
	OVERLAPPED			   m_HttpOverlapped{};
	HttpSysServer&		   m_HttpServer;
	HttpSysRequestHandler* m_HttpHandler{nullptr};
	RwLock				   m_Lock;

private:
	struct InitialRequestHandler : public HttpSysRequestHandler
	{
		inline PHTTP_REQUEST HttpRequest() { return (PHTTP_REQUEST)m_RequestBuffer; }
		inline uint32_t		 RequestBufferSize() const { return sizeof m_RequestBuffer; }

		InitialRequestHandler(HttpTransaction& InRequest) : HttpSysRequestHandler(InRequest) {}
		~InitialRequestHandler() {}

		virtual void				   IssueRequest() override;
		virtual HttpSysRequestHandler* HandleCompletion(ULONG IoResult, ULONG_PTR NumberOfBytesTransferred) override;

		PHTTP_REQUEST m_HttpRequestPtr = (HTTP_REQUEST*)(m_RequestBuffer);
		UCHAR		  m_RequestBuffer[16384 + sizeof(HTTP_REQUEST)];
	} m_InitialHttpHandler{*this};
};

//////////////////////////////////////////////////////////////////////////

class HttpMessageResponseRequest : public HttpSysRequestHandler
{
public:
	HttpMessageResponseRequest(HttpTransaction& InRequest, uint16_t ResponseCode);
	HttpMessageResponseRequest(HttpTransaction& InRequest, uint16_t ResponseCode, const char* Message);
	HttpMessageResponseRequest(HttpTransaction& InRequest, uint16_t ResponseCode, const void* Payload, size_t PayloadSize);
	HttpMessageResponseRequest(HttpTransaction& InRequest, uint16_t ResponseCode, std::span<IoBuffer> Blobs);
	~HttpMessageResponseRequest();

	virtual void				   IssueRequest() override;
	virtual HttpSysRequestHandler* HandleCompletion(ULONG IoResult, ULONG_PTR NumberOfBytesTransferred) override;

	void SuppressResponseBody();

private:
	std::vector<HTTP_DATA_CHUNK> m_HttpDataChunks;
	uint64_t					 m_TotalDataSize = 0;  // Sum of all chunk sizes

	uint16_t m_HttpResponseCode	   = 0;
	uint32_t m_NextDataChunkOffset = 0;	 // This is used for responses where the number of chunks exceed the maximum number for one API call
	uint32_t m_RemainingChunkCount = 0;
	bool	 m_IsInitialResponse   = true;

	void Initialize(uint16_t ResponseCode, std::span<IoBuffer> Blobs);

	std::vector<IoBuffer> m_DataBuffers;
};

HttpMessageResponseRequest::HttpMessageResponseRequest(HttpTransaction& InRequest, uint16_t ResponseCode) : HttpSysRequestHandler(InRequest)
{
	std::array<IoBuffer, 0> buffers;

	Initialize(ResponseCode, buffers);
}

HttpMessageResponseRequest::HttpMessageResponseRequest(HttpTransaction& InRequest, uint16_t ResponseCode, const char* Message)
: HttpSysRequestHandler(InRequest)
{
	IoBuffer				MessageBuffer(IoBuffer::Wrap, Message, strlen(Message));
	std::array<IoBuffer, 1> buffers({MessageBuffer});

	Initialize(ResponseCode, buffers);
}

HttpMessageResponseRequest::HttpMessageResponseRequest(HttpTransaction& InRequest,
													   uint16_t			ResponseCode,
													   const void*		Payload,
													   size_t			PayloadSize)
: HttpSysRequestHandler(InRequest)
{
	IoBuffer				MessageBuffer(IoBuffer::Wrap, Payload, PayloadSize);
	std::array<IoBuffer, 1> buffers({MessageBuffer});

	Initialize(ResponseCode, buffers);
}

HttpMessageResponseRequest::HttpMessageResponseRequest(HttpTransaction& InRequest, uint16_t ResponseCode, std::span<IoBuffer> Blobs)
: HttpSysRequestHandler(InRequest)
{
	Initialize(ResponseCode, Blobs);
}

HttpMessageResponseRequest::~HttpMessageResponseRequest()
{
}

void
HttpMessageResponseRequest::Initialize(uint16_t ResponseCode, std::span<IoBuffer> Blobs)
{
	m_HttpResponseCode = ResponseCode;

	const uint32_t ChunkCount = (uint32_t)Blobs.size();

	m_HttpDataChunks.resize(ChunkCount);
	m_DataBuffers.reserve(ChunkCount);

	for (IoBuffer& Buffer : Blobs)
	{
		m_DataBuffers.emplace_back(std::move(Buffer)).MakeOwned();
	}

	// Initialize the full array up front

	uint64_t LocalDataSize = 0;

	{
		PHTTP_DATA_CHUNK ChunkPtr = m_HttpDataChunks.data();

		for (IoBuffer& Buffer : m_DataBuffers)
		{
			const ULONG BufferDataSize = (ULONG)Buffer.Size();

			ZEN_ASSERT(BufferDataSize);

			IoBufferFileReference FileRef;
			if (Buffer.GetFileReference(/* out */ FileRef))
			{
				ChunkPtr->DataChunkType									   = HttpDataChunkFromFileHandle;
				ChunkPtr->FromFileHandle.FileHandle						   = FileRef.FileHandle;
				ChunkPtr->FromFileHandle.ByteRange.StartingOffset.QuadPart = FileRef.FileChunkOffset;
				ChunkPtr->FromFileHandle.ByteRange.Length.QuadPart		   = BufferDataSize;
			}
			else
			{
				ChunkPtr->DataChunkType			  = HttpDataChunkFromMemory;
				ChunkPtr->FromMemory.pBuffer	  = (void*)Buffer.Data();
				ChunkPtr->FromMemory.BufferLength = BufferDataSize;
			}
			++ChunkPtr;

			LocalDataSize += BufferDataSize;
		}
	}

	m_RemainingChunkCount = ChunkCount;
	m_TotalDataSize		  = LocalDataSize;
}

void
HttpMessageResponseRequest::SuppressResponseBody()
{
	m_RemainingChunkCount = 0;
	m_HttpDataChunks.clear();
	m_DataBuffers.clear();
}

HttpSysRequestHandler*
HttpMessageResponseRequest::HandleCompletion(ULONG IoResult, ULONG_PTR NumberOfBytesTransferred)
{
	ZEN_UNUSED(NumberOfBytesTransferred);
	ZEN_UNUSED(IoResult);

	if (m_RemainingChunkCount == 0)
		return nullptr;	 // All done

	return this;
}

void
HttpMessageResponseRequest::IssueRequest()
{
	HttpTransaction&	Tx		= Transaction();
	HTTP_REQUEST* const HttpReq = Tx.HttpRequest();
	PTP_IO const		Iocp	= Tx.Iocp();

	StartThreadpoolIo(Iocp);

	// Split payload into batches to play well with the underlying API

	const int MaxChunksPerCall = 9999;

	const int ThisRequestChunkCount	 = std::min<int>(m_RemainingChunkCount, MaxChunksPerCall);
	const int ThisRequestChunkOffset = m_NextDataChunkOffset;

	m_RemainingChunkCount -= ThisRequestChunkCount;
	m_NextDataChunkOffset += ThisRequestChunkCount;

	ULONG SendFlags = 0;

	if (m_RemainingChunkCount)
	{
		// We need to make more calls to send the full amount of data
		SendFlags |= HTTP_SEND_RESPONSE_FLAG_MORE_DATA;
	}

	ULONG SendResult = 0;

	if (m_IsInitialResponse)
	{
		// Populate response structure

		HTTP_RESPONSE HttpResponse = {};

		HttpResponse.EntityChunkCount = USHORT(ThisRequestChunkCount);
		HttpResponse.pEntityChunks	  = m_HttpDataChunks.data() + ThisRequestChunkOffset;

		// Content-length header

		char ContentLengthString[32];
		_ui64toa_s(m_TotalDataSize, ContentLengthString, sizeof ContentLengthString, 10);

		PHTTP_KNOWN_HEADER ContentLengthHeader = &HttpResponse.Headers.KnownHeaders[HttpHeaderContentLength];
		ContentLengthHeader->pRawValue		   = ContentLengthString;
		ContentLengthHeader->RawValueLength	   = (USHORT)strlen(ContentLengthString);

		// Content-type header

		PHTTP_KNOWN_HEADER ContentTypeHeader = &HttpResponse.Headers.KnownHeaders[HttpHeaderContentType];

		ContentTypeHeader->pRawValue	  = "application/octet-stream"; /* TODO! We must respect the content type specified */
		ContentTypeHeader->RawValueLength = (USHORT)strlen(ContentTypeHeader->pRawValue);

		HttpResponse.StatusCode	  = m_HttpResponseCode;
		HttpResponse.pReason	  = ReasonStringForHttpResultCode(m_HttpResponseCode);
		HttpResponse.ReasonLength = (USHORT)strlen(HttpResponse.pReason);

		// Cache policy

		HTTP_CACHE_POLICY CachePolicy;

		CachePolicy.Policy		  = HttpCachePolicyNocache;	 //  HttpCachePolicyUserInvalidates;
		CachePolicy.SecondsToLive = 0;

		// Initial response API call

		SendResult = HttpSendHttpResponse(Tx.RequestQueueHandle(),
										  HttpReq->RequestId,
										  SendFlags,
										  &HttpResponse,
										  &CachePolicy,
										  NULL,
										  NULL,
										  0,
										  Tx.Overlapped(),
										  NULL);

		m_IsInitialResponse = false;
	}
	else
	{
		// Subsequent response API calls

		SendResult = HttpSendResponseEntityBody(Tx.RequestQueueHandle(),
												HttpReq->RequestId,
												SendFlags,
												(USHORT)ThisRequestChunkCount,				// EntityChunkCount
												&m_HttpDataChunks[ThisRequestChunkOffset],	// EntityChunks
												NULL,										// BytesSent
												NULL,										// Reserved1
												0,											// Reserved2
												Tx.Overlapped(),							// Overlapped
												NULL										// LogData
		);
	}

	if ((SendResult != NO_ERROR)			 // Synchronous completion, but the completion event will still be posted to IOCP
		&& (SendResult != ERROR_IO_PENDING)	 // Asynchronous completion
	)
	{
		// Some error occurred, no completion will be posted

		CancelThreadpoolIo(Iocp);

		spdlog::error("failed to send HTTP response (error: {}) URL: {}", SendResult, HttpReq->pRawUrl);

		throw HttpServerException("Failed to send HTTP response", SendResult);
	}
}

//////////////////////////////////////////////////////////////////////////

class HttpSysServer
{
	friend class HttpTransaction;

public:
	HttpSysServer(WinIoThreadPool& InThreadPool);
	~HttpSysServer();

	void Initialize(const wchar_t* UrlPath);
	void Run(bool TestMode);

	void RequestExit() { m_ShutdownEvent.Set(); }

	void StartServer();
	void StopServer();

	void OnHandlingRequest();
	void IssueNewRequestMaybe();

	inline bool IsOk() const { return m_IsOk; }

	void AddEndpoint(const char* Endpoint, HttpService& Service);
	void RemoveEndpoint(const char* Endpoint, HttpService& Service);

private:
	bool			 m_IsOk				 = false;
	bool			 m_IsHttpInitialized = false;
	WinIoThreadPool& m_ThreadPool;

	std::wstring		   m_BaseUri;  // http://*:nnnn/
	HTTP_SERVER_SESSION_ID m_HttpSessionId		= 0;
	HTTP_URL_GROUP_ID	   m_HttpUrlGroupId		= 0;
	HANDLE				   m_RequestQueueHandle = 0;
	std::atomic_int32_t	   m_PendingRequests{0};
	int32_t				   m_MinPendingRequests = 4;
	int32_t				   m_MaxPendingRequests = 32;
	Event				   m_ShutdownEvent;
};

HttpSysServer::HttpSysServer(WinIoThreadPool& InThreadPool) : m_ThreadPool(InThreadPool)
{
	ULONG Result = HttpInitialize(HTTPAPI_VERSION_2, HTTP_INITIALIZE_SERVER, nullptr);

	if (Result != NO_ERROR)
	{
		return;
	}

	m_IsHttpInitialized = true;
	m_IsOk				= true;
}

HttpSysServer::~HttpSysServer()
{
	if (m_IsHttpInitialized)
	{
		HttpTerminate(HTTP_INITIALIZE_SERVER, nullptr);
	}
}

void
HttpSysServer::Initialize(const wchar_t* UrlPath)
{
	// check(bIsOk);

	ULONG Result = HttpCreateServerSession(HTTPAPI_VERSION_2, &m_HttpSessionId, 0);

	if (Result != NO_ERROR)
	{
		// Flag error

		return;
	}

	Result = HttpCreateUrlGroup(m_HttpSessionId, &m_HttpUrlGroupId, 0);

	if (Result != NO_ERROR)
	{
		// Flag error

		return;
	}

	m_BaseUri = UrlPath;

	Result = HttpAddUrlToUrlGroup(m_HttpUrlGroupId, UrlPath, /* #TODO UrlContext */ HTTP_URL_CONTEXT(0), 0);

	if (Result != NO_ERROR)
	{
		// Flag error

		return;
	}

	HTTP_BINDING_INFO HttpBindingInfo = {{0}, 0};

	Result = HttpCreateRequestQueue(HTTPAPI_VERSION_2, NULL, NULL, 0, &m_RequestQueueHandle);

	if (Result != NO_ERROR)
	{
		// Flag error!

		return;
	}

	HttpBindingInfo.Flags.Present	   = 1;
	HttpBindingInfo.RequestQueueHandle = m_RequestQueueHandle;

	Result = HttpSetUrlGroupProperty(m_HttpUrlGroupId, HttpServerBindingProperty, &HttpBindingInfo, sizeof(HttpBindingInfo));

	if (Result != NO_ERROR)
	{
		// Flag error!

		return;
	}

	// Create I/O completion port

	m_ThreadPool.CreateIocp(m_RequestQueueHandle, HttpTransaction::IoCompletionCallback, this);

	// Check result!
}

void
HttpSysServer::StartServer()
{
	int RequestCount = 32;

	for (int i = 0; i < RequestCount; ++i)
	{
		IssueNewRequestMaybe();
	}
}

void
HttpSysServer::Run(bool TestMode)
{
	if (TestMode == false)
	{
		printf("Zen Server running. Press ESC or Q to quit\n");
	}

	bool KeepRunning = true;

	do
	{
		int WaitTimeout = -1;

		if (!TestMode)
		{
			WaitTimeout = 1000;
		}

		if (!TestMode && _kbhit() != 0)
		{
			char c = (char)_getch();

			if (c == 27 || c == 'Q' || c == 'q')
			{
				RequestApplicationExit(0);
			}
		}

		m_ShutdownEvent.Wait(WaitTimeout);
	} while (!IsApplicationExitRequested());
}

void
HttpSysServer::OnHandlingRequest()
{
	--m_PendingRequests;

	if (m_PendingRequests > m_MinPendingRequests)
	{
		// We have more than the minimum number of requests pending, just let someone else
		// enqueue new requests
		return;
	}

	IssueNewRequestMaybe();
}

void
HttpSysServer::IssueNewRequestMaybe()
{
	if (m_PendingRequests.load(std::memory_order::relaxed) >= m_MaxPendingRequests)
	{
		return;
	}

	std::unique_ptr<HttpTransaction> Request = std::make_unique<HttpTransaction>(*this);

	Request->IssueInitialRequest();

	// This may end up exceeding the MaxPendingRequests limit, but it's not
	// really a problem. I'm doing it this way mostly to avoid dealing with
	// exceptions here
	++m_PendingRequests;

	Request.release();
}

void
HttpSysServer::StopServer()
{
}

void
HttpSysServer::AddEndpoint(const char* UrlPath, HttpService& Service)
{
	if (UrlPath[0] == '/')
	{
		++UrlPath;
	}

	const std::wstring Path16 = UTF8_to_wstring(UrlPath);
	Service.SetUriPrefixLength(Path16.size() + 1 /* leading slash */);

	// Convert to wide string

	std::wstring Url16 = m_BaseUri + Path16;

	ULONG Result = HttpAddUrlToUrlGroup(m_HttpUrlGroupId, Url16.c_str(), HTTP_URL_CONTEXT(&Service), 0 /* Reserved */);

	if (Result != NO_ERROR)
	{
		spdlog::error("HttpAddUrlToUrlGroup failed with result {}", Result);

		return;
	}
}

void
HttpSysServer::RemoveEndpoint(const char* UrlPath, HttpService& Service)
{
	ZEN_UNUSED(Service);

	if (UrlPath[0] == '/')
	{
		++UrlPath;
	}

	const std::wstring Path16 = UTF8_to_wstring(UrlPath);

	// Convert to wide string

	std::wstring Url16 = m_BaseUri + Path16;

	ULONG Result = HttpRemoveUrlFromUrlGroup(m_HttpUrlGroupId, Url16.c_str(), 0);

	if (Result != NO_ERROR)
	{
		spdlog::error("HttpRemoveUrlFromUrlGroup failed with result {}", Result);
	}
}

//////////////////////////////////////////////////////////////////////////

class HttpSysServerRequest : public HttpServerRequest
{
public:
	HttpSysServerRequest(HttpTransaction& Tx, HttpService& Service) : m_HttpTx(Tx)
	{
		PHTTP_REQUEST HttpRequestPtr = Tx.HttpRequest();

		const int PrefixLength	= Service.UriPrefixLength();
		const int AbsPathLength = HttpRequestPtr->CookedUrl.AbsPathLength / sizeof(char16_t);

		if (AbsPathLength >= PrefixLength)
		{
			// We convert the URI immediately because most of the code involved prefers to deal
			// with utf8. This has some performance impact which I'd prefer to avoid but for now
			// we just have to live with it

			WideToUtf8({(char16_t*)HttpRequestPtr->CookedUrl.pAbsPath + PrefixLength, gsl::narrow<size_t>(AbsPathLength - PrefixLength)},
					   m_Uri);
		}
		else
		{
			m_Uri.Reset();
		}

		if (auto QueryStringLength = HttpRequestPtr->CookedUrl.QueryStringLength)
		{
			--QueryStringLength;

			WideToUtf8({(char16_t*)(HttpRequestPtr->CookedUrl.pQueryString) + 1, QueryStringLength / sizeof(char16_t)}, m_QueryString);
		}
		else
		{
			m_QueryString.Reset();
		}

		switch (HttpRequestPtr->Verb)
		{
			case HttpVerbOPTIONS:
				m_Verb = HttpVerb::kOptions;
				break;

			case HttpVerbGET:
				m_Verb = HttpVerb::kGet;
				break;

			case HttpVerbHEAD:
				m_Verb = HttpVerb::kHead;
				break;

			case HttpVerbPOST:
				m_Verb = HttpVerb::kPost;
				break;

			case HttpVerbPUT:
				m_Verb = HttpVerb::kPut;
				break;

			case HttpVerbDELETE:
				m_Verb = HttpVerb::kDelete;
				break;

			case HttpVerbCOPY:
				m_Verb = HttpVerb::kCopy;
				break;

			default:
				// TODO: invalid request?
				m_Verb = (HttpVerb)0;
				break;
		}

		auto&			 clh = HttpRequestPtr->Headers.KnownHeaders[HttpHeaderContentLength];
		std::string_view cl(clh.pRawValue, clh.RawValueLength);

		std::from_chars(cl.data(), cl.data() + cl.size(), m_ContentLength);
	}

	~HttpSysServerRequest() {}

	virtual IoBuffer ReadPayload() override
	{
		// This is presently synchronous for simplicity, but we
		// need to implement an asynchronous version also

		HTTP_REQUEST* const HttpReq = m_HttpTx.HttpRequest();

		IoBuffer buffer(m_ContentLength);

		uint64_t BytesToRead = m_ContentLength;

		uint8_t* ReadPointer = (uint8_t*)buffer.Data();

		// First deal with any payload which has already been copied
		// into our request buffer

		const int EntityChunkCount = HttpReq->EntityChunkCount;

		for (int i = 0; i < EntityChunkCount; ++i)
		{
			HTTP_DATA_CHUNK& EntityChunk = HttpReq->pEntityChunks[i];

			ZEN_ASSERT(EntityChunk.DataChunkType == HttpDataChunkFromMemory);

			const uint64_t BufferLength = EntityChunk.FromMemory.BufferLength;

			ZEN_ASSERT(BufferLength <= BytesToRead);

			memcpy(ReadPointer, EntityChunk.FromMemory.pBuffer, BufferLength);

			ReadPointer += BufferLength;
			BytesToRead -= BufferLength;
		}

		// Call http.sys API to receive the remaining data

		while (BytesToRead)
		{
			ULONG BytesRead = 0;

			ULONG ApiResult = HttpReceiveRequestEntityBody(m_HttpTx.RequestQueueHandle(),
														   HttpReq->RequestId,
														   0, /* Flags */
														   ReadPointer,
														   (ULONG)BytesToRead,
														   &BytesRead,
														   NULL /* Overlapped */
			);

			if (ApiResult != NO_ERROR && ApiResult != ERROR_HANDLE_EOF)
			{
				throw HttpServerException("payload read failed", ApiResult);
			}

			BytesToRead -= BytesRead;
			ReadPointer += BytesRead;
		}

		return buffer;
	}

	virtual void WriteResponse(HttpResponse HttpResponseCode) override
	{
		ZEN_ASSERT(m_IsHandled == false);

		m_Response = new HttpMessageResponseRequest(m_HttpTx, (uint16_t)HttpResponseCode);

		if (m_SuppressBody)
		{
			m_Response->SuppressResponseBody();
		}

		m_IsHandled = true;
	}

	virtual void WriteResponse(HttpResponse HttpResponseCode, HttpContentType ContentType, std::span<IoBuffer> Blobs) override
	{
		ZEN_ASSERT(m_IsHandled == false);
		ZEN_UNUSED(ContentType);

		m_Response = new HttpMessageResponseRequest(m_HttpTx, (uint16_t)HttpResponseCode, Blobs);

		if (m_SuppressBody)
		{
			m_Response->SuppressResponseBody();
		}

		m_IsHandled = true;
	}

	virtual void WriteResponse(HttpResponse HttpResponseCode, HttpContentType ContentType, std::u8string_view ResponseString) override
	{
		ZEN_ASSERT(m_IsHandled == false);
		ZEN_UNUSED(ContentType);

		m_Response = new HttpMessageResponseRequest(m_HttpTx, (uint16_t)HttpResponseCode, ResponseString.data(), ResponseString.size());

		if (m_SuppressBody)
		{
			m_Response->SuppressResponseBody();
		}

		m_IsHandled = true;
	}

	HttpTransaction&			m_HttpTx;
	HttpMessageResponseRequest* m_Response = nullptr;
};

//////////////////////////////////////////////////////////////////////////

PTP_IO
HttpTransaction::Iocp()
{
	return m_HttpServer.m_ThreadPool.Iocp();
}

HANDLE
HttpTransaction::RequestQueueHandle()
{
	return m_HttpServer.m_RequestQueueHandle;
}

void
HttpTransaction::IssueInitialRequest()
{
	m_InitialHttpHandler.IssueRequest();
}

HttpTransaction::Status
HttpTransaction::HandleCompletion(ULONG IoResult, ULONG_PTR NumberOfBytesTransferred)
{
	// We use this to ensure sequential execution of completion handlers
	// for any given transaction.
	RwLock::ExclusiveLockScope _(m_Lock);

	bool RequestPending = false;

	if (HttpSysRequestHandler* CurrentHandler = m_HttpHandler)
	{
		const bool IsInitialRequest = (CurrentHandler == &m_InitialHttpHandler);

		if (IsInitialRequest)
		{
			// Ensure we have a sufficient number of pending requests outstanding
			m_HttpServer.OnHandlingRequest();
		}

		m_HttpHandler = CurrentHandler->HandleCompletion(IoResult, NumberOfBytesTransferred);

		if (m_HttpHandler)
		{
			try
			{
				m_HttpHandler->IssueRequest();

				RequestPending = true;
			}
			catch (std::exception& Ex)
			{
				spdlog::error("exception caught from IssueRequest(): {}", Ex.what());

				// something went wrong, no request is pending
			}
		}
		else
		{
			if (IsInitialRequest == false)
			{
				delete CurrentHandler;
			}
		}
	}

	m_HttpServer.IssueNewRequestMaybe();

	if (RequestPending)
	{
		return Status::kRequestPending;
	}

	return Status::kDone;
}

//////////////////////////////////////////////////////////////////////////

void
HttpTransaction::InitialRequestHandler::IssueRequest()
{
	PTP_IO Iocp = Transaction().Iocp();

	StartThreadpoolIo(Iocp);

	HttpTransaction& Tx = Transaction();

	HTTP_REQUEST* HttpReq = Tx.HttpRequest();

	ULONG Result = HttpReceiveHttpRequest(Tx.RequestQueueHandle(),
										  HTTP_NULL_ID,
										  HTTP_RECEIVE_REQUEST_FLAG_COPY_BODY,
										  HttpReq,
										  RequestBufferSize(),
										  NULL,
										  Tx.Overlapped());

	if (Result != ERROR_IO_PENDING && Result != NO_ERROR)
	{
		CancelThreadpoolIo(Iocp);

		if (Result == ERROR_MORE_DATA)
		{
			// ProcessReceiveAndPostResponse(pIoRequest, pServerContext->Io, ERROR_MORE_DATA);
		}

		// CleanupHttpIoRequest(pIoRequest);

		fprintf(stderr, "HttpReceiveHttpRequest failed, error 0x%lx\n", Result);

		return;
	}
}

HttpSysRequestHandler*
HttpTransaction::InitialRequestHandler::HandleCompletion(ULONG IoResult, ULONG_PTR NumberOfBytesTransferred)
{
	ZEN_UNUSED(IoResult);
	ZEN_UNUSED(NumberOfBytesTransferred);

	// Route requests

	try
	{
		if (HttpService* Service = reinterpret_cast<HttpService*>(m_HttpRequestPtr->UrlContext))
		{
			HttpSysServerRequest ThisRequest(Transaction(), *Service);

			Service->HandleRequest(ThisRequest);

			if (!ThisRequest.IsHandled())
			{
				return new HttpMessageResponseRequest(Transaction(), 404, "Not found");
			}

			if (ThisRequest.m_Response)
			{
				return ThisRequest.m_Response;
			}
		}

		// Unable to route
		return new HttpMessageResponseRequest(Transaction(), 404, "Item unknown");
	}
	catch (std::exception& ex)
	{
		// TODO provide more meaningful error output

		return new HttpMessageResponseRequest(Transaction(), 500, ex.what());
	}
}

//////////////////////////////////////////////////////////////////////////

struct HttpServer::Impl : public RefCounted
{
	WinIoThreadPool m_ThreadPool;
	HttpSysServer	m_HttpServer;

	Impl(int ThreadCount) : m_ThreadPool(ThreadCount), m_HttpServer(m_ThreadPool) {}

	void Initialize(int BasePort)
	{
		using namespace std::literals;

		WideStringBuilder<64> BaseUri;
		BaseUri << u8"http://*:"sv << int64_t(BasePort) << u8"/"sv;

		m_HttpServer.Initialize(BaseUri.c_str());
		m_HttpServer.StartServer();
	}

	void Run(bool TestMode) { m_HttpServer.Run(TestMode); }

	void RequestExit() { m_HttpServer.RequestExit(); }

	void Cleanup() { m_HttpServer.StopServer(); }

	void AddEndpoint(const char* Endpoint, HttpService& Service) { m_HttpServer.AddEndpoint(Endpoint, Service); }

	void AddEndpoint(const char* endpoint, std::function<void(HttpServerRequest&)> handler)
	{
		ZEN_UNUSED(endpoint, handler);

		ZEN_NOT_IMPLEMENTED();
	}
};

HttpServer::HttpServer()
{
	m_Impl = new Impl(32);
}

HttpServer::~HttpServer()
{
	m_Impl->Cleanup();
}

void
HttpServer::AddEndpoint(HttpService& Service)
{
	m_Impl->AddEndpoint(Service.BaseUri(), Service);
}

void
HttpServer::AddEndpoint(const char* endpoint, std::function<void(HttpServerRequest&)> handler)
{
	m_Impl->AddEndpoint(endpoint, handler);
}

void
HttpServer::Initialize(int BasePort)
{
	m_Impl->Initialize(BasePort);
}

void
HttpServer::Run(bool TestMode)
{
	m_Impl->Run(TestMode);
}

void
HttpServer::RequestExit()
{
	m_Impl->RequestExit();
}

//////////////////////////////////////////////////////////////////////////

void
HttpRequestRouter::AddPattern(const char* Id, const char* Regex)
{
	ZEN_ASSERT(m_PatternMap.find(Id) == m_PatternMap.end());

	m_PatternMap.insert({Id, Regex});
}

void
HttpRequestRouter::RegisterRoute(const char* Regex, HttpRequestRouter::HandlerFunc_t&& HandlerFunc, HttpVerb SupportedVerbs)
{
	// Expand patterns

	ExtendableStringBuilder<128> ExpandedRegex;

	size_t RegexLen = strlen(Regex);

	for (size_t i = 0; i < RegexLen;)
	{
		bool matched = false;

		if (Regex[i] == '{' && ((i == 0) || (Regex[i - 1] != '\\')))
		{
			// Might have a pattern reference - find closing brace

			for (size_t j = i + 1; j < RegexLen; ++j)
			{
				if (Regex[j] == '}')
				{
					std::string Pattern(&Regex[i + 1], j - i - 1);

					if (auto it = m_PatternMap.find(Pattern); it != m_PatternMap.end())
					{
						ExpandedRegex.Append(it->second.c_str());
					}
					else
					{
						// Default to anything goes (or should this just be an error?)

						ExpandedRegex.Append("(.+?)");
					}

					// skip ahead
					i = j + 1;

					matched = true;

					break;
				}
			}
		}

		if (!matched)
		{
			ExpandedRegex.Append(Regex[i++]);
		}
	}

	m_Handlers.emplace_back(ExpandedRegex.c_str(), SupportedVerbs, std::move(HandlerFunc), Regex);
}

bool
HttpRequestRouter::HandleRequest(zen::HttpServerRequest& Request)
{
	const HttpVerb Verb = Request.RequestVerb();

	std::string_view  Uri = Request.RelativeUri();
	HttpRouterRequest RouterRequest(Request);

	for (const auto& Handler : m_Handlers)
	{
		if ((Handler.Verbs & Verb) == Verb && regex_match(begin(Uri), end(Uri), RouterRequest.m_Match, Handler.RegEx))
		{
			Handler.Handler(RouterRequest);

			return true;  // Route matched
		}
	}

	return false;  // No route matched
}

TEST_CASE("http")
{
	using namespace std::literals;

	SUBCASE("router")
	{
		HttpRequestRouter r;
		r.AddPattern("a", "[[:alpha:]]+");
		r.RegisterRoute(
			"{a}",
			[&](auto) {},
			HttpVerb::kGet);

		// struct TestHttpServerRequest : public HttpServerRequest
		//{
		//	TestHttpServerRequest(std::string_view Uri) : m_uri{Uri} {}
		//};

		// TestHttpServerRequest req{};
		// r.HandleRequest(req);
	}
}

void
http_forcelink()
{
}

}  // namespace zen

