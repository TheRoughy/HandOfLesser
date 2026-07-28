#pragma once

#include "../openxr/openxr_state.h"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace HOL::state
{
	// Runtime identity and tracking transport are separate: SteamVR can be selected while its
	// driver supplies tracking without creating an OpenXR session.
	enum class TrackingProvider : uint8_t
	{
		OpenXR,
		SteamVRDriver,
		VirtualDesktopSharedMemory,
	};

	// Readiness of the selected provider, independent of the OpenXR session lifecycle.
	enum class TrackingProviderState : uint8_t
	{
		Waiting,
		Active,
		Failed,
	};

	struct TrackingState
	{
		bool isMultimodalEnabled = false;
		bool isHighFidelityEnabled = false;
	};

	struct RuntimeState
	{
		bool isVDXR = false;
		bool isOVR = false;
		bool isSteamVR = false;
		bool supportsBodyTracking = false;
		bool supportsHandTrackingAim = false;
		bool supportsHandTrackingDataSource = false;
		TrackingProvider trackingProvider = TrackingProvider::OpenXR;
		TrackingProviderState trackingProviderState = TrackingProviderState::Waiting;
		HOL::OpenXR::OpenXrState openxrState = HOL::OpenXR::OpenXrState::Uninitialized;
		char runtimeName[128] = {};
	};
} // namespace HOL::state
