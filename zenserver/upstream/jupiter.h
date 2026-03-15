// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <zencore/refcount.h>
#include <zencore/thread.h>

#include <atomic>
#include <list>
#include <memory>

namespace zen {
namespace detail {
	struct CloudCacheSessionState;
}

class IoBuffer;
class CloudCacheClient;
struct IoHash;

/**
 * Cached access token, for use with `Authorization:` header
 */
struct CloudCacheAccessToken
{
	std::string GetAuthorizationHeaderValue();
	void		SetToken(std::string_view Token);

	inline uint32_t GetSerial() const { return m_Serial.load(std::memory_order::memory_order_relaxed); }

private:
	RwLock				  m_Lock;
	std::string			  m_Token;
	std::atomic<uint32_t> m_Serial;
};

/**
 * Context for performing Jupiter operations
 *
 * Maintains an HTTP connection so that subsequent operations don't need to go
 * through the whole connection setup process
 *
 */
class CloudCacheSession
{
public:
	CloudCacheSession(CloudCacheClient* OuterClient);
	~CloudCacheSession();

	IoBuffer Get(std::string_view BucketId, std::string_view Key);
	void	 Put(std::string_view BucketId, std::string_view Key, IoBuffer Data);

private:
	RefPtr<CloudCacheClient>		m_CacheClient;
	detail::CloudCacheSessionState* m_SessionState;
};

/**
 * Jupiter upstream cache client
 */
class CloudCacheClient : public RefCounted
{
public:
	CloudCacheClient(std::string_view ServiceUrl,
					 std::string_view Namespace,
					 std::string_view OAuthProvider,
					 std::string_view OAuthClientId,
					 std::string_view OAuthSecret);
	~CloudCacheClient();

	bool			 AcquireAccessToken(std::string& AuthorizationHeaderValue);
	std::string_view Namespace() const { return m_Namespace; }
	std::string_view DefaultBucket() const { return m_DefaultBucket; }
	std::string_view ServiceUrl() const { return m_ServiceUrl; }

private:
	bool				  m_IsValid = false;
	std::string			  m_ServiceUrl;
	std::string			  m_OAuthDomain;
	std::string			  m_OAuthUriPath;
	std::string			  m_OAuthFullUri;
	std::string			  m_Namespace;
	std::string			  m_DefaultBucket;
	std::string			  m_OAuthClientId;
	std::string			  m_OAuthSecret;
	CloudCacheAccessToken m_AccessToken;

	RwLock									   m_SessionStateLock;
	std::list<detail::CloudCacheSessionState*> m_SessionStateCache;

	detail::CloudCacheSessionState* AllocSessionState();
	void							FreeSessionState(detail::CloudCacheSessionState*);

	friend class CloudCacheSession;
};

}  // namespace zen

