#pragma once

#include "src/hand/hand.h"
#include <cstddef>
#include <cstdint>
#include <src/settings/settings.h>
#include "src/steamvr/skeletal_input_joints.h"
#include "src/state/state.h"
#include "src/controller/controller.h"
#include "src/version.h"

namespace HOL
{
	inline constexpr size_t NativePacketReadChunkSize = 2 * 1024;
	inline constexpr size_t MaxNativePacketPayloadSize = 1024 * 1024;

	enum class NativePacketType : __int32
	{
		HandTransform = 0,
		ControllerInput,
		FloatInput,
		BoolInput,
		SkeletalInput,
		MultimodalPose,
		BodyTrackerPose,
		Settings,
		State,
		DriverInitialized,
		DriverStatus,
		DeviceState,
		DeviceInputInfo,
		AppInitialized,
		AppShutdownRequested,
		InvalidPacket
	};

	struct NativePacket
	{
		NativePacketType packetType = NativePacketType::InvalidPacket;
		uint32_t payloadSize = 0;
	};

	// these'll ultimately handle all controller inputs
	struct FloatInputPayload
	{
		HOL::HandSide side = HOL::HandSide::LeftHand;
		char inputName[64];
		float value = 0;
	};

	struct BoolInputPayload
	{
		HOL::HandSide side = HOL::HandSide::LeftHand;
		char inputName[64];
		bool value = 0;
	};

	struct ControllerInputPayload
	{
		bool valid = 0;
		HOL::HandSide side = HOL::HandSide::LeftHand;

		float triggerValue = 0.0f;
		bool triggerTouch = false;
		bool triggerClick = false;

		float gripTouch = 0.0f;
		float gripValue = 0.0f; // Like a trigger, if it had a grip trigger?
		float gripForce = 0.0f; // Squeeze? This triggers vrchat grip

		bool systemClick = false;

		float fingerCurlIndex = 0.0f;
		float fingerCurlMiddle = 0.0f;
		float fingerCurlRing = 0.0f;
		float fingerCurlPinky = 0.0f;
	};

	struct SkeletalPayload
	{
		HOL::HandSide side;
		HOL::PoseLocation
			locations[SteamVR::HandSkeletonBone::eBone_Count]; // HandSkeletonBone::eBone_Count
	};

	struct MultimodalPosePayload
	{
		// Controller pose, transformed from palm position
		HOL::PoseLocation leftHandPose;
		HOL::PoseLocation rightHandPose;

		// Tracked flags
		bool leftHandTracked = false;
		bool rightHandTracked = false;
	};

	// Mimics vr::DriverPose_t, but only contains the bits we care about.
	// Subject to change.
	struct HandTransformPayload
	{
		bool active = false;  // Got a result
		bool valid = false;	  // Is a proper pose of some kind
		bool tracked = false; // Is directly tracked instead of inferred or frozen
		bool stale = false;
		HOL::HandSide side = HandSide::HandSide_MAX;
		HOL::PoseLocation location;
		HOL::PoseVelocity velocity;
	};

	struct BodyTrackerPosePayload
	{
		HOL::BodyTrackerRole role;
		HOL::PoseLocation location;
		HOL::PoseVelocity velocity;
		bool active = false;
		bool valid = false;
		bool tracked = false;
	};

	struct StatePayload
	{
		HOL::state::TrackingState tracking;
		HOL::state::RuntimeState runtime;
	};

	struct DriverInitializedPayload
	{
		char driverVersion[64] = HOL_VERSION_STRING; // For future version checks
		uint32_t capabilities = 0;		  // Bitfield for future features
	};

	struct DriverStatusPayload
	{
		bool emulatedControllersActive = false;
		bool hasNormalControllers = false;
		bool hasHandTrackingControllers = false;
		int hookedControllerCount = 0;
		int emulatedTrackerCount = 0;
	};

	struct DeviceStatePayload
	{
		char serial[128] = {};
		vr::ETrackedDeviceClass role = vr::TrackedDeviceClass_Invalid;
		vr::EVRSkeletalTrackingLevel trackingLevel = vr::VRSkeletalTracking_Estimated;
		bool nativePoseIsValid = false;
		bool nativeDeviceIsConnected = false;
		vr::ETrackingResult nativeTrackingResult = vr::TrackingResult_Uninitialized;
		uint64_t nativePoseAgeMs = 0;
	};

	struct DeviceInputInfoPayload
	{
		static constexpr uint32_t MaxButtonsPerDevice = 16;
		char serial[128] = {};
		uint32_t buttonCount = 0;
		char buttonPaths[MaxButtonsPerDevice][64] = {};
	};

	struct AppInitializedPayload
	{
		char appVersion[64] = HOL_VERSION_STRING; // For future version checks
		uint32_t capabilities = 0;	   // Bitfield for future features
	};

} // namespace HOL
