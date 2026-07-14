#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN // Exclude winsock.h to avoid conflicts with winsock2.h
#endif
#include <windows.h>
#include <mutex>
#include "itransport.h"

namespace HOL
{

	enum class PipeRole
	{
		Server, // Driver side - creates pipe and waits for connections
		Client	// App side - connects to existing pipe
	};

	class NamedPipeTransport : public ITransport
	{
	public:
		NamedPipeTransport() = default;
		~NamedPipeTransport();

		// ITransport interface - now takes configuration parameters
		bool init(PipeRole role, const char* pipeName = R"(\\.\pipe\HandOfLesser)");
		void shutdown() override;
		size_t send(const char* buffer, size_t size) override;
		size_t receive(char* buffer, size_t maxSize) override;
		bool isConnected() const override
		{
			return mConnected;
		}

		// Named-pipe-specific methods (not in interface)
		bool waitForConnection(DWORD timeoutMs = 1000);
		bool reconnect(); // Shutdown and re-init

	private:
		HANDLE mPipe = INVALID_HANDLE_VALUE;
		PipeRole mRole;
		OVERLAPPED mReadOverlapped = {};
		OVERLAPPED mWriteOverlapped = {};
		bool mConnected = false;
		char mPipeName[256] = {};
		std::mutex mSendMutex;

		// Helper methods
		bool createServerPipe();
		bool connectClientPipe();
		void cleanup();
		bool isPermanentError(DWORD error) const;
	};

} // namespace HOL
