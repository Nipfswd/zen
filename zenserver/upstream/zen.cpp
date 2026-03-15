// Copyright Noah Games, Inc. All Rights Reserved.

#include "zen.h"

#include <zencore/compactbinarybuilder.h>
#include <zencore/compactbinaryvalidation.h>
#include <zencore/fmtutils.h>
#include <zencore/stream.h>

#include <spdlog/spdlog.h>
#include <xxhash.h>
#include <gsl/gsl-lite.hpp>

namespace zen {

namespace detail {
	struct MessageHeader
	{
		static const uint32_t kMagic = 0x11'99'77'22;

		uint32_t Magic		 = kMagic;
		uint32_t Checksum	 = 0;
		uint16_t MessageSize = 0;  // Size *including* this field and the reserved field
		uint16_t Reserved	 = 0;

		void SetPayload(const void* PayloadData, uint64_t PayloadSize)
		{
			memcpy(Payload(), PayloadData, PayloadSize);
			MessageSize = gsl::narrow<uint16_t>(PayloadSize + sizeof MessageSize + sizeof Reserved);
			Checksum	= ComputeChecksum();
		}

		inline CbObject GetMessage() const
		{
			if (IsOk())
			{
				MemoryView MessageView(Payload(), MessageSize - sizeof MessageSize - sizeof Reserved);

				CbValidateError ValidationResult = ValidateCompactBinary(MessageView, CbValidateMode::All);

				if (ValidationResult == CbValidateError::None)
				{
					return CbObject{SharedBuffer::MakeView(MessageView)};
				}
			}

			return {};
		}

		uint32_t	TotalSize() const { return MessageSize + sizeof Checksum + sizeof Magic; }
		uint32_t	ComputeChecksum() const { return gsl::narrow_cast<uint32_t>(XXH3_64bits(&MessageSize, MessageSize)); }
		inline bool IsOk() const { return Magic == kMagic && Checksum == ComputeChecksum(); }

	private:
		inline void*	   Payload() { return &Reserved + 1; }
		inline const void* Payload() const { return &Reserved + 1; }
	};
}  // namespace detail

// Note that currently this just implements an UDP echo service for testing purposes

Mesh::Mesh(asio::io_context& IoContext) : m_IoContext(IoContext)
{
}

Mesh::~Mesh()
{
	Stop();
}

void
Mesh::Start(uint16_t Port)
{
	ZEN_ASSERT(Port);
	ZEN_ASSERT(m_Port == 0);

	m_Port		= Port;
	m_UdpSocket = std::make_unique<asio::ip::udp::socket>(m_IoContext, asio::ip::udp::endpoint(asio::ip::udp::v4(), m_Port));
	m_Thread	= std::make_unique<std::thread>([this] { Run(); });
};

void
Mesh::Stop()
{
	using namespace std::literals;

	if (!m_Port)
	{
		// Never started, nothing to do here
		return;
	}

	CbObjectWriter Msg;
	Msg << "bye"sv << m_SessionId;
	BroadcastPacket(Msg);

	m_State = kExiting;

	std::error_code Ec;
	m_Timer.cancel(Ec);

	m_UdpSocket->close(Ec);

	m_IoContext.stop();

	if (m_Thread)
	{
		m_Thread->join();
		m_Thread.reset();
	}
}

void
Mesh::EnqueueTick()
{
	m_Timer.expires_after(std::chrono::seconds(10));

	m_Timer.async_wait([&](const std::error_code& Ec) {
		if (!Ec)
		{
			OnTick();
		}
		else
		{
			if (m_State != kExiting)
			{
				spdlog::warn("Mesh timer error: {}", Ec.message());
			}
		}
	});
}

void
Mesh::OnTick()
{
	using namespace std::literals;

	CbObjectWriter Msg;

	// Basic service information

	Msg.BeginArray("s");
	Msg << m_SessionId << m_Port << /* event sequence # */ uint32_t(0);
	Msg.EndArray();

	BroadcastPacket(Msg);

	EnqueueTick();
}

void
Mesh::BroadcastPacket(CbObjectWriter& Obj)
{
	std::error_code ErrorCode;

	asio::ip::udp::socket BroadcastSocket(m_IoContext);
	BroadcastSocket.open(asio::ip::udp::v4(), ErrorCode);

	if (!ErrorCode)
	{
		BroadcastSocket.set_option(asio::ip::udp::socket::reuse_address(true));
		BroadcastSocket.set_option(asio::socket_base::broadcast(true));

		asio::ip::udp::endpoint BroadcastEndpoint(asio::ip::address_v4::broadcast(), m_Port);

		uint8_t				   MessageBuffer[kMaxMessageSize];
		detail::MessageHeader* Message = reinterpret_cast<detail::MessageHeader*>(MessageBuffer);
		*Message					   = {};

		MemoryOutStream MemOut;
		BinaryWriter	Writer(MemOut);

		Obj.Save(Writer);

		// TODO: check that it fits in a packet!

		Message->SetPayload(MemOut.Data(), MemOut.Size());

		BroadcastSocket.send_to(asio::buffer(Message, Message->TotalSize()), BroadcastEndpoint);
		BroadcastSocket.close();
	}
	else
	{
		spdlog::warn("failed to open broadcast socket: {}", ErrorCode.message());
	}
}

void
Mesh::Run()
{
	m_State = kRunning;

	EnqueueTick();

	IssueReceive();
	m_IoContext.run();
}

void
Mesh::IssueReceive()
{
	using namespace std::literals;

	m_UdpSocket->async_receive_from(
		asio::buffer(m_MessageBuffer, sizeof m_MessageBuffer),
		m_SenderEndpoint,
		[this](std::error_code ec, size_t BytesReceived) {
			if (!ec && BytesReceived)
			{
				std::error_code ErrorCode;
				std::string		Sender = m_SenderEndpoint.address().to_string(ErrorCode);

				// Process message

				uint32_t& Magic = *reinterpret_cast<uint32_t*>(m_MessageBuffer);

				switch (Magic)
				{
					case detail::MessageHeader::kMagic:
						{
							detail::MessageHeader& Header = *reinterpret_cast<detail::MessageHeader*>(m_MessageBuffer);

							if (CbObject Msg = Header.GetMessage())
							{
								const asio::ip::address& Ip = m_SenderEndpoint.address();

								if (auto Field = Msg["s"sv])
								{
									// Announce

									CbArrayView Ci = Field.AsArrayView();
									auto		It = Ci.CreateViewIterator();

									const Oid SessionId = It->AsObjectId();

									if (SessionId != Oid::Zero && SessionId != m_SessionId)
									{
										const uint16_t Port = (++It)->AsUInt16(m_SenderEndpoint.port());
										const uint32_t Lsn	= (++It)->AsUInt32();

										spdlog::info("received hey from {} ({})", Sender, SessionId);

										RwLock::ExclusiveLockScope _(m_SessionsLock);

										PeerInfo& Info = m_KnownPeers[SessionId];

										Info.LastSeen  = std::time(nullptr);
										Info.SessionId = SessionId;

										if (std::find(begin(Info.SeenOnIP), end(Info.SeenOnIP), Ip) == Info.SeenOnIP.end())
										{
											Info.SeenOnIP.push_back(Ip);
										}
									}
								}
								else if (auto Bye = Msg["bye"sv])
								{
									Oid SessionId = Field.AsObjectId();

									spdlog::info("received bye from {} ({})", Sender, SessionId);

									// We could verify that it's sent from a known IP before erasing the
									// session, if we want to be paranoid

									RwLock::ExclusiveLockScope _(m_SessionsLock);

									m_KnownPeers.erase(SessionId);
								}
								else
								{
									// Unknown message type, just ignore
								}
							}
							else
							{
								spdlog::warn("received malformed message from {}", Sender);
							}
						}
						break;

					default:
						spdlog::warn("received malformed data from {}", Sender);
						break;
				}

				IssueReceive();
			}
		});
}

}  // namespace zen

