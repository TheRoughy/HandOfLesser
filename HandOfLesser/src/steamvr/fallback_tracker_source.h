#pragma once

#include "src/hand/hand.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace vr
{
	class IVRSystem;
	struct TrackedDevicePose_t;
} // namespace vr

namespace HOL::SteamVR
{
	// Reads the poses applications see, after calibration add-ons have transformed the trackers.
	// OpenVR client interfaces stay out of the header because shared HOL headers use the driver
	// API.
	class FallbackTrackerSource
	{
	public:
		using TrackerPoses = std::array<std::optional<HOL::PoseLocation>, HOL::HandSide_MAX>;

		~FallbackTrackerSource();
		FallbackTrackerSource() = default;
		FallbackTrackerSource(const FallbackTrackerSource&) = delete;
		FallbackTrackerSource& operator=(const FallbackTrackerSource&) = delete;

		// sourceHmdPose is the real HMD forwarded by the driver, not an estimated body-head joint.
		TrackerPoses update(const std::array<std::string, HOL::HandSide_MAX>& serials,
							const HOL::PoseLocation* sourceHmdPose);
		void reset();
		// Re-establish the standing-to-tracking transform after SteamVR's origin changes.
		void resetAlignment();

	private:
		bool connect();
		void findTrackers(const std::array<std::string, HOL::HandSide_MAX>& serials);
		static std::optional<HOL::PoseLocation> toPoseLocation(const vr::TrackedDevicePose_t& pose);

		vr::IVRSystem* mSystem = nullptr;
		std::chrono::steady_clock::time_point mNextConnectAttempt{};
		std::chrono::steady_clock::time_point mNextDeviceRefresh{};
		std::array<std::string, HOL::HandSide_MAX> mSerials{};
		std::array<uint32_t, HOL::HandSide_MAX> mDeviceIds{};
		// Keep this fixed between origin changes. Recomputing it every tick would turn differences
		// in HMD sample timing into unwanted tracker movement as the user moves their head.
		std::optional<HOL::PoseLocation> mTrackingFromStanding;
		bool mReportedWaiting = false;
	};
} // namespace HOL::SteamVR
