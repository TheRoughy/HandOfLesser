#pragma once

#include <Eigen/Core>
#include <Eigen/Geometry>
#include <cstdint>
#include <array>
#include <src/controller/controller.h>
#include <map>
#include <string>
#include <vector>
#include "openvr_driver.h"

namespace HOL
{
	namespace settings
	{
		struct FingerBendSettings
		{
			// clang-format off
			float commonCurlCenter[3] = {
				50.3f, 
				44.7f,
				27.0f
			};

			float thumbCurlCenter[3] = {
				20.1f, 
				31.9f, 
				50.2f
			};

			float fingerSplayCenter[5] = {
				2.8f,	// Index
				-3.6f,
				12.9f, 
				16.2f, 
				-9.2f		// Thumb
			};

			// Range values must match unity!
			MotionRange commonCurlRange[3] = {
				{-1.3, 1}, 
				{-1, 1}, 
				{-1, 1}
			};
			MotionRange thumbCurlRange[3] = {
				{-4.1f, 0.4	}, 
				{-1.5, 1.0	},
				{-2, 1.27	}
			};

			MotionRange fingersplayRange[5] = {
				{-2,	1	},	// Index
				{-2,	1	},	// middle	
				{-3,	1	},	// ring
				{-3,	1.5f},	// little
				{-2,	1.5f}	// thumb
			};

			// clang-format on

			Eigen::Vector3f ThumbAxisOffset = Eigen::Vector3f(0, 0, -89);
		};

		struct SkeletalBendSettings
		{
			// clang-format off
			// clang-format off
			float commonCurlCenter[3] = {
				7.0f, 
				0.0f,
				0.0f
			};

			float thumbCurlCenter[3] = {
				-21.5f, 
				-34.6f, 
				2.5f
			};

			float fingerSplayCenter[5] = {
				-2.5f,// Index
				0.0f,
				-4.5f,
				-4.0f,
				-5.5f		// Thumb
			};


			// Range values must match unity!
			MotionRange commonCurlRange[3] = {
				{30.f,  -100.f }, 
				{30.f,  -100.f }, 
				{30.f,  -100.f }
			};
			MotionRange thumbCurlRange[3] = {
				{	75,	 -25.f		},
				{	0,	-100.f   },			
				{	25, -90.f	},
			};
			MotionRange fingersplayRange[5] = {
				{-25.0f,  60.0f  }, // index
				{-25.0f,  60.0f  },
				{ -60.0f, 25.0f  },
				{ -60.0f, 25.0f  },
				{-40.0f,  40.0f   } // thumb
			};

			// clang-format on
		};

		struct GeneralSettings
		{
			int motionPredictionMS = 15; // Ignored by VD, reasonable for Oculus
			int updateIntervalMS = 5;
			int minTrackedJointsForQuality
				= 26; // Minimum tracked joints to consider tracking valid
		};

		struct HandPoseSettings
		{
			// NoControllerMode should always be the default so the driver does nothing
			// until the app connects.
			HOL::ControllerMode controllerMode = HOL::ControllerMode::NoControllerMode;
			HOL::PossessionBehavior possessionBehavior
				= HOL::PossessionBehavior::PossessionBehavior_Full;
			bool applyBaseOffset = true;
			bool forceInactive = false;
			HOL::EmulatedControllerProfile emulatedControllerProfile
				= HOL::EmulatedControllerProfile::EmulatedControllerProfile_OculusTouch;
			Eigen::Vector3f orientationOffset = Eigen::Vector3f(0, 0, 0);
			Eigen::Vector3f positionOffset = Eigen::Vector3f(0, 0, 0);
		};

		struct VRChatSettings
		{
			bool sendOsc = true;
			bool sendFull = true;
			bool sendAlternating = false;
			bool sendPacked = true;

			bool interlacePacked = true;

			// Should always be 100 except for testing
			int packedUpdateInterval = 100;

			// Modify splay to work with humanoid rig
			bool useUnityHumanoidSplay = true;
			// Values we can adjust to send curl/spray to vrchat
			// without having to be in VR
			bool sendDebugOsc = false;
			bool alternateCurlTest = false; // flip between -1 and 1 every 100ms
			float curlDebug = 0;
			float splayDebug = 0;
		};

		struct VisualizerSettings
		{
			bool followLeftHand;
			bool followRightHand;
			bool showBodyTrackingPalmAxes = false;
			bool showBodyTrackingJointAxes = false;
			bool showHandTrackingPalmAxes = false;
			bool showHandTrackingJointAxes = false;
			bool showBodyTrackerAxes = false;
			bool showLookAtModifierCone = false;
			bool showInFrontModifierCone = false;
			bool showPalmFacingModifierCone = false;
			bool showControllerPositionTrails = false;
		};

		struct DebugSettings
		{
			Eigen::Vector3f rotationFix = Eigen::Vector3f(0, 0, 0);
			Eigen::Vector3f rotationOut = Eigen::Vector3f(0, 0, 0);
		};

		enum class JoystickReferenceMode
		{
			Chest = 0,
			Head,
			Hand
		};

		// Gesture kind the user selects for a binding
		enum class GestureKind
		{
			None = 0,
			Proximity, // Touch thumb + one other finger
			Chain,     // Tap a configurable finger sequence
			Grip,      // Curl middle+ring+little
			SystemAim  // Oculus system gesture (head-facing)
		};

		enum class GestureModifier : uint32_t
		{
			ClosedHand = 1 << 0,
			Hold = 1 << 1,
			InFrontOfUser = 1 << 2,
			LookingAtHand = 1 << 3,
			PalmFacingUser = 1 << 4
		};

		inline bool hasGestureModifier(uint32_t modifiers, GestureModifier modifier)
		{
			return (modifiers & static_cast<uint32_t>(modifier)) != 0;
		}

		inline void setGestureModifier(
			uint32_t& modifiers, GestureModifier modifier, bool enabled)
		{
			if (enabled)
			{
				modifiers |= static_cast<uint32_t>(modifier);
			}
			else
			{
				modifiers &= ~static_cast<uint32_t>(modifier);
			}
		}

		inline void setInvertedGestureModifier(
			uint32_t& invertedModifiers, GestureModifier modifier, bool enabled)
		{
			if (enabled)
			{
				invertedModifiers |= static_cast<uint32_t>(modifier);
			}
			else
			{
				invertedModifiers &= ~static_cast<uint32_t>(modifier);
			}
		}

		// Input target — determines gesture kind compatibility and action type.
		enum class InputTarget
		{
			None = 0,
			// Analog inputs (Grip/Trigger have both click + continuous value)
			Grip,
			Trigger,
			// Binary button inputs (click/touch only)
			A,
			B,
			X,
			Y,
			System,
			Menu,
			Thumbrest,
			Toggle_SteamVRInput,
			// Joystick (X/Z axes + touch — requires Proximity gesture)
			Joystick,
			InputTarget_MAX
		};

		struct GestureBinding;

		// Returns the default set of gesture bindings matching the previous hard-coded bindings.
		std::vector<GestureBinding> defaultGestureBindings();

		// A single configurable gesture binding
		struct GestureBinding
		{
			static constexpr int MaxChainLength = 4;

			bool enabled = true;
			HOL::HandSide side = HOL::LeftHand;

			// Gesture definition
			GestureKind kind = GestureKind::None;
			HOL::FingerType proximityFinger
				= HOL::FingerIndex;         // Finger for Proximity kind (Index/Middle/Ring/Little)
			std::array<HOL::FingerType, MaxChainLength> chainFingers = {
				HOL::FingerIndex,
				HOL::FingerMiddle,
				HOL::FingerRing,
				HOL::FingerLittle};
			int chainLength = MaxChainLength;
			uint32_t modifiers = 0; // Gesture modifiers.
			uint32_t invertedModifiers = 0; // Modifiers whose output should be inverted.

			// Output target — determines action/sink type.
			InputTarget target = InputTarget::None;
			bool pressAndRelease = false;
		};

		struct InputSettings
		{
			bool sendOscInput = true;
			JoystickReferenceMode joystickReferenceMode = JoystickReferenceMode::Head;
			int chainGestureTimeoutMS = 500;
			int holdDurationMS = 1000;
			int gateLagTimeMS = 10;
			int pinchDistanceMM = 20;
			float inFrontFovDegrees = 120.0f;
			float lookAtFovDegrees = 45.0f;
			float palmFacingFovDegrees = 80.0f;
			std::vector<GestureBinding> gestureBindings = defaultGestureBindings();
		};

		struct OpenXRSettings
		{
			std::string runtimeOverridePath;
		};

		struct SkeletalInput
		{
			vr::EVRSkeletalTrackingLevel trackingLevel = vr::VRSkeletalTracking_Full;
			float jointLengthMultiplier = 1.05f;
			bool augmentControllerSkeleton = false;
			Eigen::Vector3f positionOffset = Eigen::Vector3f(-0.168f, -0.041f, 0.049f);
			Eigen::Vector3f orientationOffset = Eigen::Vector3f(174.300f, 1.221f, 136.930f);
		};

		struct SteamVRPoseSmoothingSettings
		{
			float steamPoseTimeOffsetMS = 15.0f;
			float positionSmoothingMS = 40.0f;
			float rotationSmoothingMS = 0.0f;
			float linearVelocityMultiplier = 1.0f;
			float angularVelocityMultiplier = 0.1f;
		};

		inline SteamVRPoseSmoothingSettings defaultSteamVRPoseSmoothingSettings(bool vdxr)
		{
			SteamVRPoseSmoothingSettings settings;
			if (!vdxr)
			{
				// OpenXR should return pre-predicted poses. We don't know the display time 
				// so this is not ideal and having SteamVR apply additional prediction causes stutter.
				// VDXR incorrectly provides poses with no prediction, so we can let SteamVR handle it.
				// After messing around I've ended up with the same default values for these,
				// but we'll keep the settings separated, as they do behave differently.
			}
			return settings;
		}

		struct SteamVRSettings
		{
			bool autoLaunchApp = true;
			bool closeAppOnSteamVRExit = true;
			bool sendSteamVRInput = true;
			bool transmitLegacyFingerCurl = true;
			bool blockControllerInputWhileHandTracking = true;
			bool disableOtherControllersWhileHandTracking = true;
			bool showDevicePoseDiagnostics = false;
			SteamVRPoseSmoothingSettings poseSmoothing = defaultSteamVRPoseSmoothingSettings(true);
			SteamVRPoseSmoothingSettings standardPoseSmoothing
				= defaultSteamVRPoseSmoothingSettings(false);
			SteamVRPoseSmoothingSettings vdxrPoseSmoothing
				= defaultSteamVRPoseSmoothingSettings(true);
			bool triggerStabilization = true;
			float triggerStabilizationSmoothingMS = 200.0f;
			float triggerStabilizationFalloffMS = 500.0f;
			float handTrackingResumeBlendMS = 500.0f;
			bool jitterLastPoseOnTrackingLoss = true;
		};

		struct BodyTrackerSettings
		{
			bool enableBodyTrackers = false;

			std::array<bool, static_cast<int>(HOL::BodyTrackerRole::TrackerRole_MAX)> enabled = {
				true, true, true, true, true, true};
		};

		struct ControllerButtonOverride
		{
			std::string buttonPath;
			bool suppressTouch = false;
		};

		struct DeviceConfig
		{
			std::string serial;
			vr::ETrackedDeviceClass role = vr::TrackedDeviceClass_Invalid;
			std::vector<std::string> touchButtons; // Runtime flag, not saved to JSON
			bool activatedThisSession = false; // Runtime flag, not saved to JSON
			vr::EVRSkeletalTrackingLevel trackingLevel
				= vr::VRSkeletalTracking_Estimated; // Runtime flag, not saved to JSON
			bool nativePoseIsValid = false;		  // Runtime flag, not saved to JSON
			bool nativeDeviceIsConnected = false; // Runtime flag, not saved to JSON
			vr::ETrackingResult nativeTrackingResult
				= vr::TrackingResult_Uninitialized; // Runtime flag, not saved to JSON
			uint64_t nativePoseAgeMs = 0;		 // Runtime flag, not saved to JSON

			// Shadow tracker settings
			bool actAsTracker = false; // Make this device appear as a tracker
			bool alsoWhenHeld = false; // Act as tracker even when held (multimodal only)
			std::vector<ControllerButtonOverride> inputOverrides;
		};

		struct DeviceSettings
		{
			// Empty means automatic selection.
			std::string preferredLeftControllerSerial;
			std::string preferredRightControllerSerial;
			std::map<std::string, DeviceConfig> devices;
		};

		struct TrackingFeaturesSettings
		{
			bool enableUpperBodyTracking = true;
			bool enableSimultaneousTracking = false;
			bool forceMultimodalHandPrimary = false;
		};

		struct HandOfLesserSettings
		{
			static constexpr int CurrentVersion = 1;

			int version = CurrentVersion;
			GeneralSettings general;
			FingerBendSettings fingerBend;
			SkeletalBendSettings skeletalBend;
			HandPoseSettings handPose;
			VRChatSettings vrchat;
			DebugSettings debug;
			SteamVRSettings steamvr;
			VisualizerSettings visualizer;
			InputSettings input;
			OpenXRSettings openxr;
			SkeletalInput skeletal;
			BodyTrackerSettings bodyTrackers;
			DeviceSettings deviceSettings;
			TrackingFeaturesSettings trackingFeatures;
		};
	} // namespace settings
} // namespace HOL
