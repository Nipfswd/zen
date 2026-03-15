// Copyright Noah Games, Inc. All Rights Reserved.

#include "jupiter.h"

#include <fmt/format.h>
#include <zencore/iobuffer.h>
#include <zencore/iohash.h>
#include <zencore/string.h>
#include <zencore/thread.h>

// For some reason, these don't seem to stick, so we disable the warnings
//#	define _SILENCE_CXX17_C_HEADER_DEPRECATION_WARNING 1
//#	define _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS		1
#pragma warning(push)
#pragma warning(disable : 4004)
#pragma warning(disable : 4996)
#include <cpr/cpr.h>
#pragma warning(pop)

#if ZEN_PLATFORM_WINDOWS
#	pragma comment(lib, "Crypt32.lib")
#	pragma comment(lib, "Wldap32.lib")
#endif

#include <spdlog/spdlog.h>
#include <json11.hpp>

using namespace std::literals;
using namespace fmt::literals;

namespace zen {

namespace detail {
	struct CloudCacheSessionState
	{
		CloudCacheSessionState(CloudCacheClient& Client) : OwnerClient(Client) {}
		~CloudCacheSessionState() {}

		void Reset()
		{
			std::string Auth;
			OwnerClient.AcquireAccessToken(Auth);

			Session.SetBody({});
			Session.SetOption(cpr::Header{{"Authorization", Auth}});
		}

		CloudCacheClient& OwnerClient;
		cpr::Session	  Session;
	};
}  // namespace detail

CloudCacheSession::CloudCacheSession(CloudCacheClient* OuterClient) : m_CacheClient(OuterClient)
{
	m_SessionState = m_CacheClient->AllocSessionState();
}

CloudCacheSession::~CloudCacheSession()
{
	m_CacheClient->FreeSessionState(m_SessionState);
}

#define TESTING_PREFIX "aaaaa"

IoBuffer
CloudCacheSession::Get(std::string_view BucketId, std::string_view Key)
{
	ExtendableStringBuilder<256> Uri;
	Uri << m_CacheClient->ServiceUrl();
	Uri << "/api/v1/c/ddc/" << m_CacheClient->Namespace() << "/" << BucketId << "/" TESTING_PREFIX << Key << ".raw";

	auto& Session = m_SessionState->Session;
	Session.SetUrl(cpr::Url{Uri.c_str()});

	cpr::Response Response = Session.Get();

	if (!Response.error)
	{
		return IoBufferBuilder::MakeCloneFromMemory(Response.text.data(), Response.text.size());
	}

	return {};
}

void
CloudCacheSession::Put(std::string_view BucketId, std::string_view Key, IoBuffer Data)
{
	ExtendableStringBuilder<256> Uri;
	Uri << m_CacheClient->ServiceUrl();
	Uri << "/api/v1/c/ddc/" << m_CacheClient->Namespace() << "/" << BucketId << "/" TESTING_PREFIX << Key;

	auto& Session = m_SessionState->Session;

	IoHash Hash = IoHash::HashMemory(Data.Data(), Data.Size());

	std::string Auth;
	m_CacheClient->AcquireAccessToken(Auth);
	Session.SetOption(cpr::Url{Uri.c_str()});
	Session.SetOption(
		cpr::Header{{"Authorization", Auth}, {"X-Jupiter-IoHash", Hash.ToHexString()}, {"Content-Type", "application/octet-stream"}});
	Session.SetOption(cpr::Body{(const char*)Data.Data(), Data.Size()});

	cpr::Response Response = Session.Put();

	if (Response.error)
	{
		spdlog::warn("PUT failed: '{}'", Response.error.message);
	}
}

//////////////////////////////////////////////////////////////////////////

std::string
CloudCacheAccessToken::GetAuthorizationHeaderValue()
{
	RwLock::SharedLockScope _(m_Lock);

	return "Bearer {}"_format(m_Token);
}

inline void
CloudCacheAccessToken::SetToken(std::string_view Token)
{
	RwLock::ExclusiveLockScope _(m_Lock);
	m_Token = Token;
	++m_Serial;
}

//////////////////////////////////////////////////////////////////////////
//
// ServiceUrl:		https://jupiter.devtools.noahgames.com
// Namespace:		ue4.ddc
// OAuthClientId:	0oao91lrhqPiAlaGD0x7
// OAuthProvider:	https://noahgames.okta.com/oauth2/auso645ojjWVdRI3d0x7/v1/token
// OAuthSecret:		-GBWjjenhCgOwhxL5yBKNJECVIoDPH0MK4RDuN7d
//

CloudCacheClient::CloudCacheClient(std::string_view ServiceUrl,
								   std::string_view Namespace,
								   std::string_view OAuthProvider,
								   std::string_view OAuthClientId,
								   std::string_view OAuthSecret)
: m_ServiceUrl(ServiceUrl)
, m_OAuthFullUri(OAuthProvider)
, m_Namespace(Namespace)
, m_DefaultBucket("default")
, m_OAuthClientId(OAuthClientId)
, m_OAuthSecret(OAuthSecret)
{
	if (!OAuthProvider.starts_with("http://"sv) && !OAuthProvider.starts_with("https://"sv))
	{
		spdlog::warn("bad provider specification: '{}' - must be fully qualified"_format(OAuthProvider).c_str());
		m_IsValid = false;

		return;
	}

	// Split into host and Uri substrings

	auto SchemePos = OAuthProvider.find("://"sv);

	if (SchemePos == std::string::npos)
	{
		spdlog::warn("Bad service URL passed to cloud cache client: '{}'", ServiceUrl);
		m_IsValid = false;

		return;
	}

	auto DomainEnd = OAuthProvider.find('/', /* also skip the :// */ SchemePos + 3);

	if (DomainEnd == std::string::npos)
	{
		spdlog::warn("Bad service URL passed to cloud cache client: '{}' no path delimiter found", ServiceUrl);
		m_IsValid = false;

		return;
	}

	m_OAuthDomain  = OAuthProvider.substr(SchemePos + 3, DomainEnd - SchemePos - 3);  // noahgames.okta.com
	m_OAuthUriPath = OAuthProvider.substr(DomainEnd + 1);							  // oauth2/..../v1/token
}

CloudCacheClient::~CloudCacheClient()
{
	RwLock::ExclusiveLockScope _(m_SessionStateLock);

	for (auto State : m_SessionStateCache)
	{
		delete State;
	}
}

bool
CloudCacheClient::AcquireAccessToken(std::string& AuthorizationHeaderValue)
{
	// TODO: check for expiration

	if (!m_IsValid)
	{
		ExtendableStringBuilder<128> OAuthFormData;
		OAuthFormData << "client_id=" << m_OAuthClientId
					  << "&scope=cache_access&grant_type=client_credentials&client_secret=" << m_OAuthSecret;

		const uint32_t CurrentSerial = m_AccessToken.GetSerial();

		static RwLock			   AuthMutex;
		RwLock::ExclusiveLockScope _(AuthMutex);

		// Protect against redundant authentication operations
		if (m_AccessToken.GetSerial() != CurrentSerial)
		{
			// TODO: this could verify that the token is actually valid and retry if not?

			return true;
		}

		std::string data{OAuthFormData};

		cpr::Response Response =
			cpr::Post(cpr::Url{m_OAuthFullUri}, cpr::Header{{"Content-Type", "application/x-www-form-urlencoded"}}, cpr::Body{data});

		std::string Body{std::move(Response.text)};

		// Parse JSON response

		std::string	 JsonError;
		json11::Json JsonResponse = json11::Json::parse(Body, /* out */ JsonError);
		if (!JsonError.empty())
		{
			spdlog::warn("failed to parse OAuth response: '{}'", JsonError);

			return false;
		}

		std::string AccessToken		  = JsonResponse["access_token"].string_value();
		int			ExpiryTimeSeconds = JsonResponse["expires_in"].int_value();

		m_AccessToken.SetToken(AccessToken);

		m_IsValid = true;
	}

	AuthorizationHeaderValue = m_AccessToken.GetAuthorizationHeaderValue();

	return true;
}

detail::CloudCacheSessionState*
CloudCacheClient::AllocSessionState()
{
	detail::CloudCacheSessionState* State = nullptr;

	if (RwLock::ExclusiveLockScope _(m_SessionStateLock); !m_SessionStateCache.empty())
	{
		State = m_SessionStateCache.front();
		m_SessionStateCache.pop_front();
	}

	if (State == nullptr)
	{
		State = new detail::CloudCacheSessionState(*this);
	}

	State->Reset();

	return State;
}

void
CloudCacheClient::FreeSessionState(detail::CloudCacheSessionState* State)
{
	RwLock::ExclusiveLockScope _(m_SessionStateLock);
	m_SessionStateCache.push_front(State);
}

}  // namespace zen

