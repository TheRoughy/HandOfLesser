#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <utility>

// This file describes the runtime instructions we recognize and the machine code
// we generate. It does not read/write another process; oculus_server_patch.cpp does.
//
// The generated trampoline implements this rule:
//   enabled = (clientPid == focusedPid);                 // original runtime rule
//   if (clientPid == HandOfLesserPid && isTrackingService)
//       enabled = true;                                  // our additional exception
//
// Registers at the patch site, established by the surrounding runtime code:
//   R15D = client PID, R13D = focused PID, R12B = group-enable result.
//   RSI  = ProxyServer object, which contains its service name.
//
// All addresses/fields below refer to the analyzed runtime layout. The search pattern
// keeps the relevant registers and field offsets fixed so layout changes fail to match.

namespace HOL::hacks::detail
{
	// Identify the original check.

	// cmp r15d,r13d; sete r12b
	// Compare client/focused PIDs, then set the enable byte to 1 if they are equal.
	inline constexpr std::array<uint8_t, 7>
		OriginalPredicate{0x45, 0x3B, 0xFD, 0x41, 0x0F, 0x94, 0xC4};

	// The check starts after the first two instructions (3 + 7 bytes) of our pattern.
	inline constexpr size_t PredicateOffset = 10;

	// -1 means a wildcard byte. Branch/call displacements can change when code moves;
	// opcodes, registers, and object-field offsets must still match exactly.
	// Keep one instruction per row so this can be compared with IDA's disassembly.
	// clang-format off
	inline constexpr int ContextPattern[] = {
		0x48, 0x8B, 0x07,                         // mov rax,[rdi]
		0x44, 0x8B, 0xB8, 0x98, 0x00, 0x00, 0x00, // mov r15d,[rax+98h]: client PID
		0x45, 0x3B, 0xFD,                         // cmp r15d,r13d: focused PID
		0x41, 0x0F, 0x94, 0xC4,                   // sete r12b: original enable result
		0x8B, 0x88, 0x88, 0x00, 0x00, 0x00,       // mov ecx,[rax+88h]
		0x8B, 0x40, 0x40,                         // mov eax,[rax+40h]
		0x0D, 0x00, 0x00, 0x00, 0xF0,             // or eax,0F0000000h
		0x3B, 0xC1,                               // cmp eax,ecx
		0x0F, 0x84, -1, -1, -1, -1,              // je rel32
		0x85, 0xC9,                               // test ecx,ecx
		0x0F, 0x84, -1, -1, -1, -1,              // je rel32
		0x48, 0x8B, 0x6C, 0x24, 0x50,             // mov rbp,[rsp+50h]
		0x48, 0x3B, 0xDD,                         // cmp rbx,rbp
		0x74, -1,                                // je rel8
		0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00, // nop
		0x44, 0x8B, 0x03,                         // mov r8d,[rbx]: state-group ID
		0x48, 0x8B, 0x07,                         // mov rax,[rdi]
		0x8B, 0x90, 0x88, 0x00, 0x00, 0x00,       // mov edx,[rax+88h]
		0x8B, 0x4E, 0x20,                         // mov ecx,[rsi+20h]
		0x90,                                     // nop
		0x45, 0x0F, 0xB6, 0xCC,                   // movzx r9d,r12b: enable argument
		0xE8, -1, -1, -1, -1,                    // call ipc_EnableServerStateGroup
		0x84, 0xC0,                               // test al,al
		0x75, -1,                                // jne rel8
		0x48, 0x8D, 0x8E, 0x68, 0x04, 0x00, 0x00  // lea rcx,[rsi+468h]: service-name string
	};
	// clang-format on

	inline bool matchesContext(std::span<const uint8_t> bytes)
	{
		if (bytes.size() < std::size(ContextPattern))
			return false;
		for (size_t i = 0; i < std::size(ContextPattern); ++i)
			if (ContextPattern[i] >= 0 && bytes[i] != ContextPattern[i])
				return false;
		return true;
	}

	// Return every full match; the caller must require exactly one before patching.
	inline std::vector<size_t> findContexts(std::span<const uint8_t> bytes)
	{
		std::vector<size_t> matches;
		for (size_t i = 0; i + std::size(ContextPattern) <= bytes.size(); ++i)
			if (matchesContext(bytes.subspan(i)))
				matches.push_back(i);
		return matches;
	}

	// Encode relative jumps.

	// x64 relative jumps measure displacement from the END of the jump instruction.
	// The destination therefore has to fit in signed 32 bits relative to "next".
	inline int32_t relativeJump(uintptr_t next, uintptr_t target)
	{
		const auto delta = static_cast<int64_t>(target) - static_cast<int64_t>(next);
		if (delta < INT32_MIN || delta > INT32_MAX)
			throw std::runtime_error("OVRServer trampoline is outside relative-jump range");
		return static_cast<int32_t>(delta);
	}

	// Named destinations inside the trampoline. Their order preserves the existing
	// emitted instruction layout; no numeric label IDs are needed in the code below.
	enum TrampolineLabel : size_t
	{
		Finish,
		EnableTracking,
		CheckBodyService,
		CheckHandService,
		LabelCount
	};

	// A small byte builder, not an assembler. emit() appends instruction bytes;
	// value() appends a typed immediate/displacement in Windows' little-endian order.
	// branch() remembers jumps whose destination has not been emitted yet.
	class Code
	{
	public:
		std::vector<uint8_t> bytes;

		void emit(std::initializer_list<uint8_t> data)
		{
			bytes.insert(bytes.end(), data);
		}

		template <typename T> void value(T value)
		{
			const auto* start = reinterpret_cast<const uint8_t*>(&value);
			bytes.insert(bytes.end(), start, start + sizeof(value));
		}

		void label(size_t label)
		{
			mLabels.at(label) = bytes.size();
		}

		void branch(std::initializer_list<uint8_t> opcode, size_t label)
		{
			emit(opcode);
			mFixups.push_back({bytes.size(), label});
			value<int32_t>(0); // Reserve four bytes; finish() fills in the real displacement.
		}

		std::vector<uint8_t> finish(uintptr_t base, uintptr_t resume)
		{
			// Resolve internal jumps now that every label's byte offset is known.
			for (const auto& fixup : mFixups)
			{
				const auto displacement
					= static_cast<int32_t>(static_cast<int64_t>(mLabels.at(fixup.destinationLabel))
										   - static_cast<int64_t>(fixup.displacementOffset) - 4);
				std::memcpy(
					bytes.data() + fixup.displacementOffset, &displacement, sizeof(displacement));
			}
			// Append the final jump back into the original runtime function.
			emit({0xE9});
			value(relativeJump(base + bytes.size() + 4, resume));
			return bytes;
		}

	private:
		struct BranchFixup
		{
			size_t displacementOffset;
			size_t destinationLabel;
		};
		std::array<size_t, LabelCount> mLabels{};
		std::vector<BranchFixup> mFixups;
	};

	// Emit the HandOfLesser PID filter.

	inline void emitClientPidFilter(Code& code, uint32_t appPid)
	{
		code.emit({0x41, 0x81, 0xFF}); // cmp r15d,appPid
		code.value(appPid);
		code.branch({0x0F, 0x85}, Finish); // jne Finish: other clients keep the original result.
	}

	// Emit exact tracking-service name comparisons.

	// At this point RAX points to the service-name characters. Compare all characters,
	// not just a prefix: other similarly named services must keep their original policy.
	inline void emitServiceNameComparison(Code& code, std::string_view name)
	{
		// Compare the first 16 characters as two eight-byte values using scratch R11.
		for (size_t offset : {size_t{0}, size_t{8}})
		{
			uint64_t part;
			std::memcpy(&part, name.data() + offset, sizeof(part));
			code.emit({0x49, 0xBB}); // mov r11, eight literal name characters
			code.value(part);
			if (offset == 0)
				code.emit({0x4C, 0x39, 0x18}); // cmp [rax],r11
			else
				code.emit({0x4C, 0x39, 0x58, 0x08}); // cmp [rax+8],r11
			code.branch({0x0F, 0x85}, Finish);		 // jne Finish: name mismatch
		}

		// BodyApiServiceServer has four remaining characters; HandInputDataServer has three.
		if (name.size() == 20)
		{
			uint32_t tail;
			std::memcpy(&tail, name.data() + 16, sizeof(tail));
			code.emit({0x81, 0x78, 0x10}); // cmp dword ptr [rax+16], final four characters
			code.value(tail);
		}
		else
		{
			uint16_t tail;
			std::memcpy(&tail, name.data() + 16, sizeof(tail));
			code.emit({0x66, 0x81, 0x78, 0x10}); // cmp word ptr [rax+16], next two characters
			code.value(tail);
			code.branch({0x0F, 0x85}, Finish);
			code.emit({0x80,
					   0x78,
					   0x12,
					   static_cast<uint8_t>(name[18])}); // cmp [rax+18], final character
		}
		code.branch({0x0F, 0x85}, Finish);
		code.branch({0xE9}, EnableTracking); // Complete exact match: allow this tracking service.
	}

	inline void emitTrackingServiceFilter(Code& code)
	{
		// ProxyServer stores an MSVC x64 std::string at RSI + 0x468:
		//   +0x468: character pointer (or inline characters for short strings)
		//   +0x478: length
		//   +0x480: capacity
		// Both target names exceed the 15-character inline limit, so require heap storage.
		code.emit({0x48, 0x83, 0xBE, 0x80, 0x04, 0x00, 0x00, 0x0F}); // cmp [rsi+480h],15
		code.branch({0x0F, 0x86}, Finish); // jbe Finish: short string cannot be a target service.

		code.emit({0x48, 0x8B, 0x86, 0x68, 0x04, 0x00, 0x00}); // mov rax,[rsi+468h]: characters
		code.emit({0x48, 0x85, 0xC0});						   // test rax,rax
		code.branch({0x0F, 0x84}, Finish);					   // je Finish: no character buffer

		// Use length to select which complete name comparison to run.
		constexpr std::string_view services[] = {"BodyApiServiceServer", "HandInputDataServer"};
		constexpr TrampolineLabel serviceLabels[] = {CheckBodyService, CheckHandService};
		for (size_t i = 0; i < std::size(services); ++i)
		{
			code.emit({0x48, 0x83, 0xBE, 0x78, 0x04, 0x00, 0x00}); // cmp [rsi+478h],name length
			code.emit({static_cast<uint8_t>(services[i].size())});
			code.branch({0x0F, 0x84}, serviceLabels[i]); // je: length matches this candidate
		}
		code.branch({0xE9}, Finish); // Neither target length: keep original result.

		for (size_t i = 0; i < std::size(services); ++i)
		{
			code.label(serviceLabels[i]);
			emitServiceNameComparison(code, services[i]);
		}
	}

	// Assemble the complete trampoline.

	inline std::vector<uint8_t> buildTrampoline(uint32_t appPid, uintptr_t base, uintptr_t resume)
	{
		if (appPid == 0)
			throw std::runtime_error("Invalid tracking client PID");
		Code code;

		// 1. Execute the displaced instructions so the original focus result remains
		//    available for every client/service that our filters reject.
		code.bytes.assign(OriginalPredicate.begin(), OriginalPredicate.end());

		// 2. Save the flags from that original comparison and our scratch registers.
		//    Our PID/name comparisons must not change the surrounding function's state.
		code.emit({0x9C, 0x50, 0x41, 0x53}); // pushfq; push rax; push r11

		// 3. All rejected clients/services jump straight to Finish.
		emitClientPidFilter(code, appPid);
		emitTrackingServiceFilter(code);

		// 4. Only an exact tracking-service match for this app reaches this label.
		code.label(EnableTracking);
		code.emit({0x41, 0xB4, 0x01}); // mov r12b,1: change only the low enable byte

		// 5. Restore scratch registers/flags and return after the seven displaced bytes.
		code.label(Finish);
		code.emit({0x41, 0x5B, 0x58, 0x9D}); // pop r11; pop rax; popfq
		return code.finish(base, resume);
	}
} // namespace HOL::hacks::detail
