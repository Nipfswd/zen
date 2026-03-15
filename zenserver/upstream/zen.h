// Copyright Noah Games, Inc. All Rights Reserved.

#pragma once

#include <zencore/memory.h>
#include <zencore/thread.h>
#include <zencore/uid.h>
#include <zencore/zencore.h>

#pragma warning(push)
#pragma warning(disable : 4127)
#include <tsl/robin_map.h>
#pragma warning(pop)

#include <asio.hpp>

#include <chrono>

namespace zen {

class CbObjectWriter;

/** Zen mesh tracker
 *
 * Discovers and tracks local peers
 */
class Mesh
{
public:
	Mesh(asio::io_context& IoContext);
	~Mesh();

	void Start(uint16_t Port);
	void Stop();

private:
	void Run();
	void IssueReceive();
	void EnqueueTick();
	void OnTick();
	void BroadcastPacket(CbObjectWriter&);

	enum State
	{
		kInitializing,
		kRunning,
		kExiting
	};

	static const int kMaxMessageSize = 2048;
	static const int kMaxUpdateSize	 = 1400;  // We'll try not to send messages larger than this

	std::atomic<State>					   m_State = kInitializing;
	asio::io_context&					   m_IoContext;
	std::unique_ptr<asio::ip::udp::socket> m_UdpSocket;
	std::unique_ptr<asio::ip::udp::socket> m_BroadcastSocket;
	asio::ip::udp::endpoint				   m_SenderEndpoint;
	std::unique_ptr<std::thread>		   m_Thread;
	uint16_t							   m_Port = 0;
	uint8_t								   m_MessageBuffer[kMaxMessageSize];
	asio::high_resolution_timer			   m_Timer{m_IoContext};
	Oid									   m_SessionId{Oid::NewOid()};

	struct PeerInfo
	{
		Oid							   SessionId;
		std::time_t					   LastSeen;
		std::vector<asio::ip::address> SeenOnIP;
	};

	RwLock									   m_SessionsLock;
	tsl::robin_map<Oid, PeerInfo, Oid::Hasher> m_KnownPeers;
};

class ZenKvCacheClient
{
public:
	ZenKvCacheClient();
	~ZenKvCacheClient();

private:
};

}  // namespace zen

