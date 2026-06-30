#pragma once

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <vector>
#include "src/packet/nativepacket.h"

namespace HOL
{
	struct NativePacketView
	{
		NativePacketType packetType = NativePacketType::InvalidPacket;
		const char* payload = nullptr;
		size_t payloadSize = 0;
		bool valid = false;

		explicit operator bool() const
		{
			return valid;
		}

		template <typename T> bool copyPayload(T& out) const
		{
			if (payload == nullptr || payloadSize != sizeof(T))
			{
				return false;
			}

			std::memcpy(&out, payload, sizeof(T));
			return true;
		}
	};

	/**
	 * Abstract base interface for all transport implementations.
	 * Provides common send/receive/lifecycle methods.
	 */
	class ITransport
	{
	public:
		virtual ~ITransport() = default;

		// Note: init() is NOT part of the interface since different transports
		// require different configuration parameters. Call init() directly on
		// the concrete transport type.

		// Shutdown and cleanup resources
		virtual void shutdown() = 0;

		// Explicit send - returns bytes sent (0 on failure)
		virtual size_t send(const char* buffer, size_t size) = 0;

		// Template send for callers that need to transmit an already-serialized buffer object.
		template <typename T> size_t send(const T& packet)
		{
			return send((const char*)&packet, sizeof(T));
		}

		// Build the exact wire message as [NativePacket header][payload]. Do not send a typed
		// envelope struct directly; padding would become part of the protocol.
		size_t sendPayloadBytes(NativePacketType type, const char* payload, size_t payloadSize)
		{
			if (payloadSize > MaxNativePacketPayloadSize
				|| payloadSize > (std::numeric_limits<uint32_t>::max)()
				|| (payloadSize > 0 && payload == nullptr))
			{
				return 0;
			}

			NativePacket packet{
				.packetType = type,
				.payloadSize = static_cast<uint32_t>(payloadSize),
			};
			std::vector<char> packetBuffer(sizeof(NativePacket) + payloadSize);
			std::memcpy(packetBuffer.data(), &packet, sizeof(packet));
			if (payloadSize > 0)
			{
				std::memcpy(packetBuffer.data() + sizeof(packet), payload, payloadSize);
			}
			return send(packetBuffer.data(), packetBuffer.size());
		}

		template <NativePacketType Type, typename Payload> size_t sendPayload(const Payload& payload)
		{
			static_assert(sizeof(Payload) <= MaxNativePacketPayloadSize);
			return sendPayloadBytes(Type, reinterpret_cast<const char*>(&payload), sizeof(payload));
		}

		template <NativePacketType Type> size_t sendPacket()
		{
			return sendPayloadBytes(Type, nullptr, 0);
		}

		// Receive raw data into buffer - returns bytes received (0 on timeout/error)
		// Implementations should support a reasonable timeout (e.g., 1 second)
		virtual size_t receive(char* buffer, size_t maxSize) = 0;

		// Receive and validate a native packet envelope. The payload pointer is valid until the
		// next receive call, and callers should copy it into the expected payload type.
		virtual NativePacketView receivePacket()
		{
			NativePacket packet;
			if (!receiveExact(reinterpret_cast<char*>(&packet), sizeof(packet)))
			{
				return {};
			}

			if (packet.payloadSize > MaxNativePacketPayloadSize)
			{
				return {};
			}

			mReceiveBuffer.resize(packet.payloadSize);
			if (packet.payloadSize > 0
				&& !receiveExact(mReceiveBuffer.data(), packet.payloadSize))
			{
				return {};
			}

			return NativePacketView{
				.packetType = packet.packetType,
				.payload = mReceiveBuffer.data(),
				.payloadSize = packet.payloadSize,
				.valid = true,
			};
		}

		// Check if transport is connected/ready
		// For connectionless protocols (UDP), always returns true after init
		virtual bool isConnected() const = 0;

	protected:
		bool receiveExact(char* buffer, size_t size)
		{
			size_t totalRead = 0;
			while (totalRead < size)
			{
				const size_t remaining = size - totalRead;
				const size_t chunkSize = (std::min)(remaining, NativePacketReadChunkSize);
				const size_t bytesRead = receive(buffer + totalRead, chunkSize);
				if (bytesRead == 0 || bytesRead > remaining)
				{
					return false;
				}
				totalRead += bytesRead;
			}
			return true;
		}

		// Buffer for receivePacket() implementation. Payload pointer remains valid until next
		// receivePacket() call.
		std::vector<char> mReceiveBuffer;
	};

} // namespace HOL
