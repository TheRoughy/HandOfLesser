#include "oculus_server_patch.h"
#include "oculus_server_patch_code.h"
#include "src/windows/windows_utils.h"

#include <tlhelp32.h>
#include <algorithm>
#include <iostream>
#include <string>
#include <utility>

// OVRServer stops publishing hand/body data to clients that are not focused.
// We change its permission check to also allow this HandOfLesser process, but only
// for the two tracking services. Other clients and services keep the original rule.
//
// The seven original instruction bytes are replaced with a jump to our "trampoline":
// a small block of machine code allocated INSIDE OVRServer. It evaluates the original
// focus rule, applies our exception, then jumps back to the instruction after the patch.
//
// Reading order:
//   Impl::install()   - find the check, prepare the trampoline, redirect execution.
//   Impl::restore()   - put the original bytes back, then free the trampoline.
//   replacePredicate()- pause server threads and change those seven bytes safely.
//   The private helpers below handle Windows processes, memory, and thread suspension.
// Machine-code generation and the search pattern are in oculus_server_patch_code.h.

namespace HOL::hacks
{
	namespace
	{
		constexpr wchar_t IpcModule[] = L"RuntimeIPCServiceClient_64.dll";
		constexpr size_t TrampolinePageSize = 4096;
		using Bytes = std::vector<uint8_t>;

		// Windows resource ownership and error reporting.

		void checkWindowsCall(bool success, const char* operation)
		{
			if (!success)
				throw std::runtime_error(std::string(operation) + ": "
										 + FormatWindowsError(GetLastError()));
		}

		// Close a Windows handle when its owner goes out of scope, including on exceptions.
		// Moving transfers ownership; copying would give two owners of the same handle.
		class OwnedHandle
		{
		public:
			explicit OwnedHandle(HANDLE value = nullptr) : value(value)
			{
			}
			~OwnedHandle()
			{
				if (value && value != INVALID_HANDLE_VALUE)
					CloseHandle(value);
			}
			OwnedHandle(const OwnedHandle&) = delete;
			OwnedHandle& operator=(const OwnedHandle&) = delete;
			OwnedHandle(OwnedHandle&& other) noexcept : value(std::exchange(other.value, nullptr))
			{
			}
			HANDLE value;
		};

		// A snapshot is Windows' temporary list of processes, DLLs, or threads.
		// ERROR_BAD_LENGTH means that list changed while Windows was constructing it.
		OwnedHandle takeSnapshot(DWORD flags, DWORD pid = 0)
		{
			for (int attempt = 0; attempt < 5; ++attempt)
			{
				HANDLE result = CreateToolhelp32Snapshot(flags, pid);
				if (result != INVALID_HANDLE_VALUE)
					return OwnedHandle(result);
				if (GetLastError() != ERROR_BAD_LENGTH || attempt == 4)
					checkWindowsCall(false, "CreateToolhelp32Snapshot");
			}
			throw std::runtime_error("Could not snapshot OVRServer");
		}

		// Locate the server and its IPC DLL.

		// Use the OVRServer belonging to our Windows login session. Refuse to guess if
		// multiple servers qualify; choosing by executable name alone is not enough.
		DWORD findServer()
		{
			DWORD ownSession;
			checkWindowsCall(ProcessIdToSessionId(GetCurrentProcessId(), &ownSession),
							 "ProcessIdToSessionId");
			auto list = takeSnapshot(TH32CS_SNAPPROCESS);
			PROCESSENTRY32W entry{sizeof(entry)};
			DWORD result = 0;
			BOOL more = Process32FirstW(list.value, &entry);
			while (more)
			{
				DWORD session;
				if (_wcsicmp(entry.szExeFile, L"OVRServer_x64.exe") == 0
					&& ProcessIdToSessionId(entry.th32ProcessID, &session) && session == ownSession)
				{
					if (result)
						throw std::runtime_error(
							"Multiple OVRServer processes in this Windows session");
					result = entry.th32ProcessID;
				}
				more = Process32NextW(list.value, &entry);
			}
			checkWindowsCall(GetLastError() == ERROR_NO_MORE_FILES, "Process32 enumeration");
			if (!result)
				throw std::runtime_error(
					"OVRServer is not running; connect Quest Link before starting HandOfLesser");
			return result;
		}

		// This returns the DLL's address in OVRServer, not a DLL loaded into HandOfLesser.
		MODULEENTRY32W findModule(DWORD pid)
		{
			auto list = takeSnapshot(TH32CS_SNAPMODULE, pid);
			MODULEENTRY32W entry{sizeof(entry)}, found{};
			BOOL more = Module32FirstW(list.value, &entry);
			while (more)
			{
				if (_wcsicmp(entry.szModule, IpcModule) == 0)
				{
					if (found.modBaseAddr)
						throw std::runtime_error("Multiple OVRServer IPC modules");
					found = entry;
				}
				more = Module32NextW(list.value, &entry);
			}
			checkWindowsCall(GetLastError() == ERROR_NO_MORE_FILES, "Module32 enumeration");
			if (!found.modBaseAddr)
				throw std::runtime_error("OVRServer IPC module is not loaded");
			return found;
		}

		// Access memory in the other process. These uintptr_t addresses belong to OVRServer;
		// HandOfLesser must use Read/WriteProcessMemory rather than dereference them.

		Bytes readRemoteBytes(HANDLE process, uintptr_t address, size_t size)
		{
			Bytes bytes(size);
			SIZE_T count;
			checkWindowsCall(
				ReadProcessMemory(
					process, reinterpret_cast<void*>(address), bytes.data(), size, &count),
				"ReadProcessMemory");
			if (count != size)
				throw std::runtime_error("Short OVRServer memory read");
			return bytes;
		}

		template <typename T> T readRemoteObject(HANDLE process, uintptr_t address)
		{
			T value;
			auto bytes = readRemoteBytes(process, address, sizeof(value));
			std::memcpy(&value, bytes.data(), sizeof(value));
			return value;
		}

		// Verify each write by reading it back. A successful API return alone is insufficient.
		void writeRemoteBytes(HANDLE process, uintptr_t address, std::span<const uint8_t> bytes)
		{
			SIZE_T count;
			checkWindowsCall(
				WriteProcessMemory(
					process, reinterpret_cast<void*>(address), bytes.data(), bytes.size(), &count),
				"WriteProcessMemory");
			if (count != bytes.size()
				|| readRemoteBytes(process, address, bytes.size())
					   != Bytes(bytes.begin(), bytes.end()))
				throw std::runtime_error("OVRServer write verification failed");
		}

		// Tell the CPU to execute the new instructions rather than any cached old bytes.
		void flushRemoteInstructions(HANDLE process, uintptr_t address, size_t size)
		{
			checkWindowsCall(FlushInstructionCache(process, reinterpret_cast<void*>(address), size),
							 "FlushInstructionCache");
		}

		// Find the instruction to patch using the DLL's executable sections.

		// PE headers describe a loaded Windows DLL: its architecture, size, and sections.
		// Validate their bounds before using any header-provided offsets to read memory.
		std::vector<IMAGE_SECTION_HEADER> readValidatedSections(HANDLE process,
																const MODULEENTRY32W& module)
		{
			const auto base = reinterpret_cast<uintptr_t>(module.modBaseAddr);
			auto dos = readRemoteObject<IMAGE_DOS_HEADER>(process, base);
			if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < sizeof(dos)
				|| static_cast<size_t>(dos.e_lfanew) + sizeof(IMAGE_NT_HEADERS64)
					   > module.modBaseSize)
				throw std::runtime_error("Invalid OVRServer IPC DOS header");

			auto nt = readRemoteObject<IMAGE_NT_HEADERS64>(process, base + dos.e_lfanew);
			if (nt.Signature != IMAGE_NT_SIGNATURE
				|| nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64
				|| nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC
				|| nt.FileHeader.SizeOfOptionalHeader != sizeof(IMAGE_OPTIONAL_HEADER64)
				|| nt.OptionalHeader.SizeOfImage != module.modBaseSize
				|| !(nt.FileHeader.Characteristics & IMAGE_FILE_DLL)
				|| nt.FileHeader.NumberOfSections == 0 || nt.FileHeader.NumberOfSections > 96)
				throw std::runtime_error("OVRServer IPC module is not a valid x64 DLL");

			const uintptr_t sectionTable = base + dos.e_lfanew + sizeof(nt);
			if (sectionTable - base + nt.FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER)
				> module.modBaseSize)
				throw std::runtime_error("Invalid OVRServer IPC section table");

			std::vector<IMAGE_SECTION_HEADER> sections;
			for (WORD i = 0; i < nt.FileHeader.NumberOfSections; ++i)
				sections.push_back(readRemoteObject<IMAGE_SECTION_HEADER>(
					process, sectionTable + i * sizeof(IMAGE_SECTION_HEADER)));
			return sections;
		}

		uintptr_t findTrackingPermissionCheck(HANDLE process, const MODULEENTRY32W& module)
		{
			const auto base = reinterpret_cast<uintptr_t>(module.modBaseAddr);
			std::vector<uintptr_t> matches;

			// Search code sections only. The pattern describes the surrounding control flow;
			// PredicateOffset selects the seven bytes inside that pattern that we replace.
			for (const auto& section : readValidatedSections(process, module))
			{
				if (!(section.Characteristics & IMAGE_SCN_MEM_EXECUTE))
					continue;
				const size_t size = section.Misc.VirtualSize;
				if (section.VirtualAddress > module.modBaseSize
					|| size > module.modBaseSize - section.VirtualAddress)
					throw std::runtime_error("Invalid OVRServer executable-section size");
				if (!size)
					continue;

				auto bytes = readRemoteBytes(process, base + section.VirtualAddress, size);
				for (size_t offset : detail::findContexts(bytes))
					matches.push_back(base + section.VirtualAddress + offset
									  + detail::PredicateOffset);
			}

			// A partial match or two complete matches cannot identify the intended check.
			if (matches.size() != 1)
				throw std::runtime_error(
					"Expected one complete OVRServer tracking predicate pattern, found "
					+ std::to_string(matches.size()) + "; no patch applied");
			return matches.front();
		}

		// Allocate space for the trampoline in OVRServer.

		// The seven-byte site has room for a five-byte JMP and two NOPs. That JMP uses
		// a signed 32-bit displacement, so its destination must be within about 2 GiB.
		// Search free regions in that range and align allocations to Windows' granularity.
		uintptr_t allocateTrampolineNear(HANDLE process, uintptr_t patchAddress)
		{
			SYSTEM_INFO system;
			GetSystemInfo(&system);
			const uintptr_t granularity = system.dwAllocationGranularity;
			const uintptr_t low = std::max(
				granularity, patchAddress > 0x7FFF0000 ? patchAddress - 0x7FFF0000 : granularity);
			const uintptr_t high
				= std::min(reinterpret_cast<uintptr_t>(system.lpMaximumApplicationAddress),
						   patchAddress + 0x7FFF0000);

			for (uintptr_t cursor = low; cursor < high;)
			{
				MEMORY_BASIC_INFORMATION info;
				checkWindowsCall(
					VirtualQueryEx(process, reinterpret_cast<void*>(cursor), &info, sizeof(info))
						!= 0,
					"VirtualQueryEx");
				const auto region = reinterpret_cast<uintptr_t>(info.BaseAddress);
				const uintptr_t end = region + info.RegionSize;
				if (end <= cursor)
					throw std::runtime_error("OVRServer address-space scan did not advance");

				if (info.State == MEM_FREE)
				{
					const uintptr_t candidate
						= (std::max(cursor, region) + granularity - 1) & ~(granularity - 1);
					if (candidate + TrampolinePageSize <= std::min(end, high))
					{
						void* memory = VirtualAllocEx(process,
													  reinterpret_cast<void*>(candidate),
													  TrampolinePageSize,
													  MEM_COMMIT | MEM_RESERVE,
													  PAGE_READWRITE);
						if (memory)
							return reinterpret_cast<uintptr_t>(memory);
					}
				}
				cursor = end;
			}
			throw std::runtime_error("No OVRServer trampoline page within relative-jump range");
		}

		// Pause threads while installing/removing executable instructions.

		// This is a retryable condition: let the thread run out of the affected code,
		// then try pausing the server again. Other errors abort the operation.
		struct BusyCode : std::runtime_error
		{
			BusyCode() : std::runtime_error("OVRServer thread is executing the patch code")
			{
			}
		};

		// Constructing this object pauses the server; destroying it resumes the threads.
		// Both normal completion and exceptions must balance ONLY our own suspend counts.
		class SuspendedThreads
		{
		public:
			SuspendedThreads(DWORD pid,
							 uintptr_t patchAddress,
							 uintptr_t trampolineAddress,
							 size_t trampolineSize)
			{
				try
				{
					pauseAllThreads(pid);
					checkInstructionPointers(patchAddress, trampolineAddress, trampolineSize);
				}
				catch (...)
				{
					// A throwing constructor never reaches its destructor.
					resumeAllThreads();
					throw;
				}
			}

			~SuspendedThreads()
			{
				resumeAllThreads();
			}

		private:
			// Keep each paused thread's ID and handle until its suspend count is restored.
			std::vector<std::pair<DWORD, OwnedHandle>> mThreads;

			bool isAlreadyPaused(DWORD tid) const
			{
				for (const auto& thread : mThreads)
					if (thread.first == tid)
						return true;
				return false;
			}

			bool pauseThread(DWORD tid)
			{
				OwnedHandle thread(OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT
												  | THREAD_QUERY_INFORMATION,
											  false,
											  tid));
				// A thread can exit between being listed and being opened.
				if (!thread.value && GetLastError() == ERROR_INVALID_PARAMETER)
					return false;
				checkWindowsCall(thread.value != nullptr, "OpenThread(OVRServer)");

				// Allocate bookkeeping first: an allocation failure after SuspendThread
				// would leave an unrecorded thread that cleanup could not resume.
				mThreads.reserve(mThreads.size() + 1);
				checkWindowsCall(SuspendThread(thread.value) != static_cast<DWORD>(-1),
								 "SuspendThread(OVRServer)");
				mThreads.emplace_back(tid, std::move(thread));
				return true;
			}

			bool pauseNewThreads(DWORD pid)
			{
				bool added = false;
				auto list = takeSnapshot(TH32CS_SNAPTHREAD);
				THREADENTRY32 entry{sizeof(entry)};
				BOOL more = Thread32First(list.value, &entry);
				while (more)
				{
					if (entry.th32OwnerProcessID == pid && !isAlreadyPaused(entry.th32ThreadID))
						if (pauseThread(entry.th32ThreadID))
							added = true;
					more = Thread32Next(list.value, &entry);
				}
				checkWindowsCall(GetLastError() == ERROR_NO_MORE_FILES, "Thread32 enumeration");
				return added;
			}

			void pauseAllThreads(DWORD pid)
			{
				// Threads may be created while the first snapshot is being processed.
				// Repeat until a snapshot contains no newly discovered server threads.
				for (int pass = 0; pass < 8; ++pass)
					if (!pauseNewThreads(pid))
					{
						if (mThreads.empty())
							throw std::runtime_error("OVRServer thread list did not stabilize");
						return;
					}
				throw std::runtime_error("OVRServer thread list did not stabilize");
			}

			void checkInstructionPointers(uintptr_t patchAddress,
										  uintptr_t trampolineAddress,
										  size_t trampolineSize)
			{
				// RIP is the address of a paused thread's next instruction. Replacing bytes
				// underneath it, or freeing its trampoline, would invalidate that instruction.
				for (const auto& [tid, thread] : mThreads)
				{
					CONTEXT context{};
					context.ContextFlags = CONTEXT_CONTROL;
					checkWindowsCall(GetThreadContext(thread.value, &context),
									 "GetThreadContext(OVRServer)");
					const bool inOriginalCheck
						= context.Rip >= patchAddress
						  && context.Rip < patchAddress + detail::OriginalPredicate.size();
					const bool inTrampoline = trampolineAddress && context.Rip >= trampolineAddress
											  && context.Rip < trampolineAddress + trampolineSize;
					if (inOriginalCheck || inTrampoline)
						throw BusyCode();
				}
			}

			void resumeAllThreads() noexcept
			{
				for (auto it = mThreads.rbegin(); it != mThreads.rend(); ++it)
					if (ResumeThread(it->second.value) == static_cast<DWORD>(-1))
						std::cerr << "OVRServer ResumeThread failed for " << it->first << ": "
								  << FormatWindowsError(GetLastError()) << std::endl;
				mThreads.clear();
			}
		};
	} // namespace

	// State belonging to one install/restore cycle. Keeping Windows details here lets
	// the public header expose just the lifecycle operations, without Windows types.
	struct OvrServerTrackingPatch::Impl
	{
		DWORD serverPid = findServer();
		OwnedHandle serverProcess{OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ
												  | PROCESS_VM_WRITE | PROCESS_VM_OPERATION
												  | SYNCHRONIZE,
											  false,
											  serverPid)};
		OwnedHandle patchMutex;
		bool ownsPatchMutex = false;

		// Both addresses refer to memory in OVRServer, not in our own process.
		uintptr_t predicateAddress = 0;
		uintptr_t trampolineAddress = 0;
		Bytes trampolineBytes;
		Bytes redirectBytes;

		// Set BEFORE attempting the redirect write. Even a failed/partial write must
		// be checked during cleanup before the trampoline can safely be freed.
		bool mayHaveChangedPredicate = false;

		Impl()
		{
			verifyServerIdentity();
			claimPatchOwnership();
		}

		~Impl()
		{
			if (ownsPatchMutex)
				ReleaseMutex(patchMutex.value);
			// OwnedHandle destructors close the process and mutex handles afterwards.
		}

		void verifyServerIdentity()
		{
			checkWindowsCall(serverProcess.value != nullptr, "OpenProcess(OVRServer)");
			wchar_t path[32768];
			DWORD size = std::size(path);
			checkWindowsCall(QueryFullProcessImageNameW(serverProcess.value, 0, path, &size),
							 "QueryFullProcessImageNameW(OVRServer)");
			const wchar_t* name = wcsrchr(path, L'\\');
			if (_wcsicmp(name ? name + 1 : path, L"OVRServer_x64.exe") != 0)
				throw std::runtime_error(
					"OVRServer PID changed identity before patch installation");
		}

		void claimPatchOwnership()
		{
			// The mutex name is shared with the Python prototype. A second app/runner must
			// not install over our redirect, or restore bytes that belong to another owner.
			const std::wstring mutexName
				= L"Local\\HandOfLesser.OvrServerPatchPrototype." + std::to_wstring(serverPid);
			patchMutex.value = CreateMutexW(nullptr, false, mutexName.c_str());
			checkWindowsCall(patchMutex.value != nullptr, "CreateMutexW(OVRServer patch)");
			const DWORD wait = WaitForSingleObject(patchMutex.value, 0);
			if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED)
				throw std::runtime_error(
					"Another HandOfLesser or prototype runner owns the OVRServer patch");
			ownsPatchMutex = true;
		}

		// Installation: locate -> prepare -> redirect. Execution only changes in step 3.

		void prepareTrampoline()
		{
			trampolineAddress = allocateTrampolineNear(serverProcess.value, predicateAddress);
			trampolineBytes
				= detail::buildTrampoline(GetCurrentProcessId(),
										  trampolineAddress,
										  predicateAddress + detail::OriginalPredicate.size());

			// Fill a writable page before any runtime instruction can jump into it.
			writeRemoteBytes(serverProcess.value, trampolineAddress, trampolineBytes);
			DWORD ignored;
			checkWindowsCall(VirtualProtectEx(serverProcess.value,
											  reinterpret_cast<void*>(trampolineAddress),
											  TrampolinePageSize,
											  PAGE_EXECUTE_READ,
											  &ignored),
							 "VirtualProtectEx(OVRServer trampoline)");
			flushRemoteInstructions(serverProcess.value, trampolineAddress, trampolineBytes.size());
		}

		void prepareRedirect()
		{
			// E9 + signed displacement is a five-byte JMP. Two NOPs fill out the seven
			// displaced bytes; the trampoline eventually resumes at predicateAddress + 7.
			redirectBytes = {0xE9};
			const int32_t displacement
				= detail::relativeJump(predicateAddress + 5, trampolineAddress);
			const auto* bytes = reinterpret_cast<const uint8_t*>(&displacement);
			redirectBytes.insert(redirectBytes.end(), bytes, bytes + sizeof(displacement));
			redirectBytes.insert(redirectBytes.end(), {0x90, 0x90});
		}

		void install()
		{
			// 1. Find the loaded DLL and its unique focus/permission check.
			auto module = findModule(serverPid);
			predicateAddress = findTrackingPermissionCheck(serverProcess.value, module);

			// 2. Prepare both pieces of code; OVRServer still executes the original check.
			prepareTrampoline();
			prepareRedirect();

			// 3. Pause the server briefly and replace the check with our jump.
			mayHaveChangedPredicate = true;
			replacePredicate(detail::OriginalPredicate, redirectBytes);

			std::cout << "Oculus background tracking patch installed in OVRServer PID " << std::dec
					  << serverPid << " for HandOfLesser PID " << GetCurrentProcessId()
					  << "; predicate " << reinterpret_cast<void*>(predicateAddress) << " (RVA 0x"
					  << std::hex
					  << predicateAddress - reinterpret_cast<uintptr_t>(module.modBaseAddr)
					  << "), trampoline " << reinterpret_cast<void*>(trampolineAddress) << std::dec
					  << std::endl;
		}

		// Change the predicate bytes. Used in both directions: original -> jump on
		// installation, jump -> original on restoration.

		void verifyExpectedPredicate(std::span<const uint8_t> expected)
		{
			if (readRemoteBytes(serverProcess.value, predicateAddress, expected.size())
				!= Bytes(expected.begin(), expected.end()))
				throw std::runtime_error(
					"OVRServer patch ownership check failed; refusing to overwrite bytes");

			const bool installing
				= std::equal(expected.begin(), expected.end(), detail::OriginalPredicate.begin());
			if (!mayHaveChangedPredicate || installing)
			{
				// Recheck the full signature after pausing, closing the gap between scanning
				// and writing. During restoration the signature contains our jump instead.
				if (!detail::matchesContext(
						readRemoteBytes(serverProcess.value,
										predicateAddress - detail::PredicateOffset,
										std::size(detail::ContextPattern))))
					throw std::runtime_error(
						"OVRServer predicate context changed before installation");
			}
		}

		void rollBackFailedWrite(std::span<const uint8_t> expected)
		{
			// Called while threads are still paused. Repair a partial write before any
			// thread can execute it. If repair fails, cleanup must retain the trampoline.
			try
			{
				writeRemoteBytes(serverProcess.value, predicateAddress, expected);
				flushRemoteInstructions(serverProcess.value, predicateAddress, expected.size());
			}
			catch (const std::exception& error)
			{
				std::cerr << "OVRServer rollback failed: " << error.what() << std::endl;
			}
		}

		void writePredicateWhilePaused(std::span<const uint8_t> expected,
									   std::span<const uint8_t> replacement)
		{
			// Make the original code page writable only for this short, verified write.
			DWORD oldProtection;
			checkWindowsCall(VirtualProtectEx(serverProcess.value,
											  reinterpret_cast<void*>(predicateAddress),
											  replacement.size(),
											  PAGE_EXECUTE_READWRITE,
											  &oldProtection),
							 "VirtualProtectEx(OVRServer predicate)");
			try
			{
				writeRemoteBytes(serverProcess.value, predicateAddress, replacement);
				flushRemoteInstructions(serverProcess.value, predicateAddress, replacement.size());
			}
			catch (...)
			{
				rollBackFailedWrite(expected);
				DWORD ignored;
				VirtualProtectEx(serverProcess.value,
								 reinterpret_cast<void*>(predicateAddress),
								 replacement.size(),
								 oldProtection,
								 &ignored);
				throw;
			}
			DWORD ignored;
			checkWindowsCall(VirtualProtectEx(serverProcess.value,
											  reinterpret_cast<void*>(predicateAddress),
											  replacement.size(),
											  oldProtection,
											  &ignored),
							 "Restore OVRServer predicate protection");
		}

		void replacePredicate(std::span<const uint8_t> expected,
							  std::span<const uint8_t> replacement)
		{
			for (int attempt = 0; attempt < 20; ++attempt)
			{
				try
				{
					// The guard resumes threads on every exit, including exceptions.
					SuspendedThreads paused(
						serverPid, predicateAddress, trampolineAddress, trampolineBytes.size());
					verifyExpectedPredicate(expected);
					writePredicateWhilePaused(expected, replacement);
					return;
				}
				catch (const BusyCode&)
				{
					// A paused thread was inside the affected code. It has now been resumed;
					// give it time to leave that code before the next attempt.
					Sleep(10);
				}
			}
			throw std::runtime_error(
				"Could not find an idle moment to change OVRServer patch code");
		}

		// Restoration: remove the jump FIRST, then release its destination.

		void restoreOriginalPredicate()
		{
			if (!mayHaveChangedPredicate)
				return;

			auto current = readRemoteBytes(
				serverProcess.value, predicateAddress, detail::OriginalPredicate.size());
			if (current == redirectBytes)
				replacePredicate(redirectBytes, detail::OriginalPredicate);
			else if (current
					 != Bytes(detail::OriginalPredicate.begin(), detail::OriginalPredicate.end()))
				throw std::runtime_error(
					"OVRServer patch bytes changed; retaining trampoline until server restart");

			// Original bytes may already be present after a successful rollback.
			mayHaveChangedPredicate = false;
			std::cout << "Original OVRServer tracking predicate restored and verified."
					  << std::endl;
		}

		void freeTrampoline()
		{
			if (!trampolineAddress)
				return;
			checkWindowsCall(VirtualFreeEx(serverProcess.value,
										   reinterpret_cast<void*>(trampolineAddress),
										   0,
										   MEM_RELEASE),
							 "VirtualFreeEx(OVRServer trampoline)");
			trampolineAddress = 0;
		}

		void restore()
		{
			// An exited server has already lost all of its memory; there is nothing to edit.
			if (WaitForSingleObject(serverProcess.value, 0) == WAIT_OBJECT_0)
			{
				std::cout << "OVRServer exited; its tracking patch memory is gone." << std::endl;
				return;
			}
			restoreOriginalPredicate();
			// Once the jump is gone and paused threads were checked, no runtime thread
			// can enter the trampoline. It is now safe to release that page.
			freeTrampoline();
		}
	};

	// Public lifecycle: translate Windows/internal failures into the app's existing logs.
	// The same owner handles normal shutdown, failed installation, and destructor cleanup.

	OvrServerTrackingPatch::OvrServerTrackingPatch() = default;

	OvrServerTrackingPatch::~OvrServerTrackingPatch()
	{
		restore();
	}

	bool OvrServerTrackingPatch::install()
	{
		if (mImpl)
			return mImpl->mayHaveChangedPredicate;
		try
		{
			mImpl = std::make_unique<Impl>();
			mImpl->install();
			return true;
		}
		catch (const std::exception& error)
		{
			std::cerr << "Oculus background tracking patch failed: " << error.what() << std::endl;
			restore();
			return false;
		}
	}

	void OvrServerTrackingPatch::restore() noexcept
	{
		if (!mImpl)
			return;
		try
		{
			mImpl->restore();
			mImpl.reset();
		}
		catch (const std::exception& error)
		{
			// Keep the owner/state so another restore attempt can still use it. In particular,
			// do not free memory that a surviving redirect might still reference.
			std::cerr << "Oculus background tracking patch cleanup failed: " << error.what()
					  << "; remote allocation retained, restart OVRServer to reset." << std::endl;
		}
	}
} // namespace HOL::hacks
