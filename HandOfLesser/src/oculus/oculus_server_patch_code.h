#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <vector>

// Discovery and code generation only; remote memory/lifecycle belong to the .cpp.
// The trampoline adds one exception to the runtime's original permission rule:
//   enabled = (clientPid == focusedPid) || (clientPid == HandOfLesserPid);
// It does not change focus ownership or another client's permission result.
// No service-name strings or C++ object layout are accessed by the trampoline.

namespace HOL::hacks::detail
{
	inline constexpr size_t PredicateOffset = 10;
	inline constexpr size_t PredicateSize = 7;
	inline constexpr size_t ContextSize = PredicateOffset + PredicateSize;
	inline constexpr size_t PermissionCallSearchSize = 128;
	inline constexpr size_t TrampolineSize = 26;

	struct Predicate
	{
		std::array<uint8_t, PredicateSize> original;
		uint8_t clientRegister;
		uint8_t enableRegister;
	};

	// Decode the two loads preceding the permission check. These survive both normal
	// patch installation and a forced app exit, so they also anchor stale-patch discovery.
	inline std::optional<uint8_t> decodeClientRegister(std::span<const uint8_t> bytes)
	{
		if (bytes.size() < PredicateOffset)
			return std::nullopt;

		// REX.W + MOV r64,[r64]. Exclude SIB/RIP-relative addressing, which changes length.
		if ((bytes[0] & 0xFA) != 0x48 || bytes[1] != 0x8B || (bytes[2] & 0xC0) != 0
			|| (bytes[2] & 7) == 4 || (bytes[2] & 7) == 5)
			return std::nullopt;
		const uint8_t objectRegister = ((bytes[2] >> 3) & 7) | ((bytes[0] & 4) << 1);

		// REX + MOV r32,[object+disp32]. The field is read by the runtime, not our code.
		if ((bytes[3] & 0xFA) != 0x40 || bytes[4] != 0x8B || (bytes[5] & 0xC0) != 0x80
			|| (bytes[5] & 7) == 4)
			return std::nullopt;
		const uint8_t baseRegister = (bytes[5] & 7) | ((bytes[3] & 1) << 3);
		const uint8_t clientRegister = ((bytes[5] >> 3) & 7) | ((bytes[3] & 4) << 1);
		if (baseRegister != objectRegister || clientRegister == 4)
			return std::nullopt;
		return clientRegister;
	}

	// Recognize four instructions and follow their register relationships:
	//   mov object,[iterator]
	//   mov clientPid,[object+field]
	//   cmp clientPid,focusedPid
	//   sete enabled
	// Decode only these forms, rather than introducing a general disassembler. The
	// CMP/SETE must occupy seven bytes so they can be replaced by our five-byte jump.
	// Different registers and field offsets are allowed; other encodings fail to match.
	// The .cpp also verifies that "enabled" feeds ipc_EnableServerStateGroup.
	inline std::optional<Predicate> decodeContext(std::span<const uint8_t> bytes)
	{
		const auto client = decodeClientRegister(bytes);
		if (bytes.size() < ContextSize || !client)
			return std::nullopt;
		const uint8_t clientRegister = *client;

		// REX + CMP r32,r32. Its left operand must be the value just loaded above.
		if ((bytes[10] & 0xFA) != 0x40 || bytes[11] != 0x3B || (bytes[12] & 0xC0) != 0xC0)
			return std::nullopt;
		const uint8_t comparedRegister = ((bytes[12] >> 3) & 7) | ((bytes[10] & 4) << 1);
		const uint8_t focusedRegister = (bytes[12] & 7) | ((bytes[10] & 1) << 3);
		if (comparedRegister != clientRegister || focusedRegister == clientRegister
			|| focusedRegister == 4)
			return std::nullopt;

		// REX + SETE r8. Require a low-byte register, never AH/CH/DH/BH or the stack pointer.
		if ((bytes[13] & 0xFE) != 0x40 || bytes[14] != 0x0F || bytes[15] != 0x94
			|| (bytes[16] & 0xF8) != 0xC0)
			return std::nullopt;
		const uint8_t enableRegister = (bytes[16] & 7) | ((bytes[13] & 1) << 3);
		// The extra PID comparison happens after SETE; its source must remain intact.
		if (enableRegister == clientRegister || enableRegister == 4)
			return std::nullopt;

		Predicate result{{}, clientRegister, enableRegister};
		std::copy_n(bytes.begin() + PredicateOffset, PredicateSize, result.original.begin());
		return result;
	}

	// Recognize only our five-byte relative jump and two padding NOPs. This does not
	// establish ownership: the caller must also validate its destination and full code.
	inline std::optional<uintptr_t> decodeRedirect(std::span<const uint8_t> context,
												   uintptr_t contextAddress)
	{
		if (context.size() < ContextSize || !decodeClientRegister(context)
			|| context[PredicateOffset] != 0xE9 || context[PredicateOffset + 5] != 0x90
			|| context[PredicateOffset + 6] != 0x90)
			return std::nullopt;
		int32_t displacement;
		std::memcpy(&displacement, context.data() + PredicateOffset + 1, sizeof(displacement));
		const auto target
			= static_cast<int64_t>(contextAddress + PredicateOffset + 5) + displacement;
		if (target <= 0)
			return std::nullopt;
		return static_cast<uintptr_t>(target);
	}

	inline std::vector<size_t> findContexts(std::span<const uint8_t> bytes)
	{
		std::vector<size_t> matches;
		for (size_t offset = 0; offset + ContextSize <= bytes.size(); ++offset)
			if (decodeContext(bytes.subspan(offset)))
				matches.push_back(offset);
		return matches;
	}

	// Independently identify the purpose of the decoded result. Within a bounded
	// window, require MOVZX r9d,enabled immediately followed by a direct call to the
	// DLL's exported group-enable function. Unrelated checks/padding between the
	// predicate and this argument setup do not become part of the search signature.
	inline bool feedsPermissionCall(std::span<const uint8_t> context,
									uintptr_t contextAddress,
									const Predicate& predicate,
									uintptr_t permissionFunction)
	{
		const size_t size = std::min(context.size(), ContextSize + PermissionCallSearchSize);
		for (size_t offset = ContextSize; offset + 9 <= size; ++offset)
		{
			if (context[offset] != (0x44 | (predicate.enableRegister >> 3))
				|| context[offset + 1] != 0x0F || context[offset + 2] != 0xB6
				|| context[offset + 3] != (0xC8 | (predicate.enableRegister & 7))
				|| context[offset + 4] != 0xE8)
				continue;
			int32_t displacement;
			std::memcpy(&displacement, context.data() + offset + 5, sizeof(displacement));
			const auto target = static_cast<int64_t>(contextAddress + offset + 9) + displacement;
			if (target == static_cast<int64_t>(permissionFunction))
				return true;
		}
		return false;
	}

	// x64 relative jumps measure displacement from the END of the instruction.
	inline int32_t relativeJump(uintptr_t next, uintptr_t target)
	{
		const auto delta = static_cast<int64_t>(target) - static_cast<int64_t>(next);
		if (delta < INT32_MIN || delta > INT32_MAX)
			throw std::runtime_error("OVRServer trampoline is outside relative-jump range");
		return static_cast<int32_t>(delta);
	}

	inline std::vector<uint8_t>
	buildTrampoline(const Predicate& predicate, uint32_t appPid, uintptr_t base, uintptr_t resume)
	{
		if (!appPid || predicate.clientRegister > 15 || predicate.enableRegister > 15
			|| predicate.clientRegister == 4 || predicate.enableRegister == 4
			|| predicate.clientRegister == predicate.enableRegister)
			throw std::runtime_error("Invalid tracking predicate or client PID");

		// Only two registers are referenced, both discovered from the original code.
		// No scratch registers or object dereferences are needed. Preserve the original
		// comparison flags, and change only the enable register's low byte for our PID.
		// clang-format off
		std::vector<uint8_t> code{
			0, 0, 0, 0, 0, 0, 0, // original CMP/SETE: retain the normal focus result
			0x9C,                 // pushfq: save flags from the original comparison
			static_cast<uint8_t>(0x40 | (predicate.clientRegister >> 3)),
			0x81, static_cast<uint8_t>(0xF8 | (predicate.clientRegister & 7)),
			0, 0, 0, 0,           // cmp clientPid,appPid (immediate filled below)
			0x75, 0x03,           // jne +3: other clients keep their original enable byte
			static_cast<uint8_t>(0x40 | (predicate.enableRegister >> 3)),
			static_cast<uint8_t>(0xB0 | (predicate.enableRegister & 7)), 0x01, // mov enabled,1
			0x9D,                 // popfq: restore original flags on either path
			0xE9, 0, 0, 0, 0      // jmp resume (displacement filled below)
		};
		// clang-format on
		std::copy(predicate.original.begin(), predicate.original.end(), code.begin());
		std::memcpy(code.data() + 11, &appPid, sizeof(appPid));
		const int32_t displacement = relativeJump(base + code.size(), resume);
		std::memcpy(code.data() + 22, &displacement, sizeof(displacement));
		return code;
	}

	struct RetainedPatch
	{
		Predicate predicate;
		uint32_t ownerPid;
	};

	// A killed app leaves its redirect/code in OVRServer. Recover the original seven
	// bytes from the start of that code, decode them against the surviving two loads,
	// and regenerate the entire trampoline. Every byte, including its return jump,
	// must agree. An arbitrary hook at the same site is never adopted by this check.
	// Process lifetime, page ownership, and the permission call are checked in the .cpp.
	inline std::optional<RetainedPatch> decodeRetainedPatch(std::span<const uint8_t> context,
															uintptr_t contextAddress,
															std::span<const uint8_t> code,
															uintptr_t trampolineAddress)
	{
		const auto redirect = decodeRedirect(context, contextAddress);
		if (!redirect || *redirect != trampolineAddress || code.size() != TrampolineSize)
			return std::nullopt;
		std::array<uint8_t, ContextSize> originalContext;
		std::copy_n(context.begin(), ContextSize, originalContext.begin());
		std::copy_n(code.begin(), PredicateSize, originalContext.begin() + PredicateOffset);
		const auto predicate = decodeContext(originalContext);
		if (!predicate)
			return std::nullopt;
		uint32_t ownerPid;
		std::memcpy(&ownerPid, code.data() + 11, sizeof(ownerPid));
		if (!ownerPid)
			return std::nullopt;
		try
		{
			const auto expected = buildTrampoline(
				*predicate, ownerPid, trampolineAddress, contextAddress + ContextSize);
			if (!std::equal(expected.begin(), expected.end(), code.begin(), code.end()))
				return std::nullopt;
		}
		catch (const std::runtime_error&)
		{
			return std::nullopt; // An unreachable return jump cannot belong to our patch.
		}
		return RetainedPatch{*predicate, ownerPid};
	}
} // namespace HOL::hacks::detail
