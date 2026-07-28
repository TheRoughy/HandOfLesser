#pragma once

#include "virtual_desktop_body_state.h"

#include <HandOfLesserCommon.h>
#include <Windows.h>

#include <array>
#include <chrono>
#include <optional>

namespace HOL::VirtualDesktop
{
	// Polls Virtual Desktop's shared state without consuming its auto-reset update event.
	class VirtualDesktopTrackingSource : public HOL::TrackingSource
	{
	public:
		VirtualDesktopTrackingSource();
		~VirtualDesktopTrackingSource() override;

		HOL::TrackingSourceFrame update(bool skeletalUpdate, bool applyBaseOffset) override;
		bool hasTrackingReference() const override;
		void reset() override;

	private:
		static constexpr auto ReconnectInterval = std::chrono::seconds(1);
		static constexpr auto StaleTimeout = std::chrono::milliseconds(100);
		// VDXR normally gets this fallback from the Oculus eye-height setting.
		static constexpr float DefaultEyeHeight = 1.675f;

		bool connect();
		void disconnect();
		bool readStableSnapshot(BodyStateV2& snapshot) const;
		void updateSamples(const BodyStateV2& snapshot, bool forceUpdate);
		void updateHandSample(HOL::HandSide side,
							  const BodyStateV2& snapshot,
							  const BodyStateV2* previous,
							  bool forceUpdate);
		void updateBodySample(const BodyStateV2& snapshot,
							  const BodyStateV2* previous,
							  bool forceUpdate);
		void updateTrackingReference(const BodyStateV2& snapshot);
		void setSamplesInactive();
		// Convert VD's eye-relative shared poses to the floor-relative space normally returned by
		// VDXR when HandOfLesser requests stage-space tracking.
		void initializeFloorOffset(const BodyStateV2& snapshot);

		HANDLE mMapping = nullptr;
		const BodyStateV2* mMappedState = nullptr;
		BodyStateV2 mLastState{};
		bool mHasState = false;
		bool mSourceFresh = false;
		bool mReportedWaiting = false;
		bool mFloorOffsetInitialized = false;
		float mFloorOffset = 0.0f;
		std::chrono::steady_clock::time_point mNextConnectAttempt{};
		std::chrono::steady_clock::time_point mLastStateChange{};

		std::array<HOL::HandTrackingSample, HOL::HandSide_MAX> mHandSamples;
		std::array<uint64_t, HOL::HandSide_MAX> mHandGenerations{};
		HOL::BodyTrackingSample mBodySample;
		uint64_t mBodyGeneration = 0;
		std::optional<HOL::PoseLocation> mHmdPose;
	};
} // namespace HOL::VirtualDesktop
