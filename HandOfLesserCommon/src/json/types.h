#pragma once

#include <nlohmann/json.hpp>
#include <src/settings/settings.h>
#include <src/json/serializer.h>

namespace HOL
{
	inline void to_json(nlohmann::json& j, const MotionRange& range)
	{
		j = nlohmann::json{{"start", range.start}, {"end", range.end}};
	}

	inline void from_json(const nlohmann::json& j, MotionRange& range)
	{
		nlohmann::get_to_if_present(j, "start", range.start);
		nlohmann::get_to_if_present(j, "end", range.end);
	}

	namespace settings
	{
		inline void to_json(nlohmann::json& j, const FingerBendSettings& settings)
		{
			j = {{"commonCurlCenter", settings.commonCurlCenter},
				 {"thumbCurlCenter", settings.thumbCurlCenter},
				 {"fingerSplayCenter", settings.fingerSplayCenter},
				 {"commonCurlRange", settings.commonCurlRange},
				 {"thumbCurlRange", settings.thumbCurlRange},
				 {"fingersplayRange", settings.fingersplayRange},
				 {"ThumbAxisOffset", settings.ThumbAxisOffset}};
		}

		inline void from_json(const nlohmann::json& j, FingerBendSettings& settings)
		{
			nlohmann::get_to_if_present(j, "commonCurlCenter", settings.commonCurlCenter);
			nlohmann::get_to_if_present(j, "thumbCurlCenter", settings.thumbCurlCenter);
			nlohmann::get_to_if_present(j, "fingerSplayCenter", settings.fingerSplayCenter);
			nlohmann::get_to_if_present(j, "commonCurlRange", settings.commonCurlRange);
			nlohmann::get_to_if_present(j, "thumbCurlRange", settings.thumbCurlRange);
			nlohmann::get_to_if_present(j, "fingersplayRange", settings.fingersplayRange);
			nlohmann::get_to_if_present(j, "ThumbAxisOffset", settings.ThumbAxisOffset);
		}

		inline void to_json(nlohmann::json& j, const SkeletalBendSettings& settings)
		{
			j = {{"commonCurlCenter", settings.commonCurlCenter},
				 {"thumbCurlCenter", settings.thumbCurlCenter},
				 {"fingerSplayCenter", settings.fingerSplayCenter},
				 {"commonCurlRange", settings.commonCurlRange},
				 {"thumbCurlRange", settings.thumbCurlRange},
				 {"fingersplayRange", settings.fingersplayRange}};
		}

		inline void from_json(const nlohmann::json& j, SkeletalBendSettings& settings)
		{
			nlohmann::get_to_if_present(j, "commonCurlCenter", settings.commonCurlCenter);
			nlohmann::get_to_if_present(j, "thumbCurlCenter", settings.thumbCurlCenter);
			nlohmann::get_to_if_present(j, "fingerSplayCenter", settings.fingerSplayCenter);
			nlohmann::get_to_if_present(j, "commonCurlRange", settings.commonCurlRange);
			nlohmann::get_to_if_present(j, "thumbCurlRange", settings.thumbCurlRange);
			nlohmann::get_to_if_present(j, "fingersplayRange", settings.fingersplayRange);
		}

		inline void to_json(nlohmann::json& j, const GeneralSettings& settings)
		{
			j = {{"motionPredictionMS", settings.motionPredictionMS},
				 {"updateIntervalMS", settings.updateIntervalMS},
				 {"minTrackedJointsForQuality", settings.minTrackedJointsForQuality}};
		}

		inline void from_json(const nlohmann::json& j, GeneralSettings& settings)
		{
			nlohmann::get_to_if_present(j, "motionPredictionMS", settings.motionPredictionMS);
			nlohmann::get_to_if_present(j, "updateIntervalMS", settings.updateIntervalMS);
			nlohmann::get_to_if_present(
				j, "minTrackedJointsForQuality", settings.minTrackedJointsForQuality);
		}

		inline void to_json(nlohmann::json& j, const HandPoseSettings& settings)
		{
			j = {{"controllerMode", settings.controllerMode},
				 {"possessionBehavior", settings.possessionBehavior},
				 {"applyBaseOffset", settings.applyBaseOffset},
				 {"emulatedControllerProfile", settings.emulatedControllerProfile},
				 {"orientationOffset", settings.orientationOffset},
				 {"positionOffset", settings.positionOffset}};
		}

		inline void from_json(const nlohmann::json& j, HandPoseSettings& settings)
		{
			nlohmann::get_to_if_present(j, "controllerMode", settings.controllerMode);
			nlohmann::get_to_if_present(
				j, "possessionBehavior", settings.possessionBehavior);
			nlohmann::get_to_if_present(j, "applyBaseOffset", settings.applyBaseOffset);
			nlohmann::get_to_if_present(
				j, "emulatedControllerProfile", settings.emulatedControllerProfile);
			nlohmann::get_to_if_present(j, "orientationOffset", settings.orientationOffset);
			nlohmann::get_to_if_present(j, "positionOffset", settings.positionOffset);
		}

		inline void to_json(nlohmann::json& j, const VRChatSettings& settings)
		{
			j = {{"sendOsc", settings.sendOsc},
				 {"sendFull", settings.sendFull},
				 {"sendAlternating", settings.sendAlternating},
				 {"sendPacked", settings.sendPacked},
				 {"interlacePacked", settings.interlacePacked},
				 {"packedUpdateInterval", settings.packedUpdateInterval},
				 {"useUnityHumanoidSplay", settings.useUnityHumanoidSplay},
				 {"sendDebugOsc", settings.sendDebugOsc},
				 {"alternateCurlTest", settings.alternateCurlTest},
				 {"curlDebug", settings.curlDebug},
				 {"splayDebug", settings.splayDebug}};
		}

		inline void from_json(const nlohmann::json& j, VRChatSettings& settings)
		{
			nlohmann::get_to_if_present(j, "sendOsc", settings.sendOsc);
			nlohmann::get_to_if_present(j, "sendFull", settings.sendFull);
			nlohmann::get_to_if_present(j, "sendAlternating", settings.sendAlternating);
			nlohmann::get_to_if_present(j, "sendPacked", settings.sendPacked);
			nlohmann::get_to_if_present(j, "interlacePacked", settings.interlacePacked);
			nlohmann::get_to_if_present(j, "packedUpdateInterval", settings.packedUpdateInterval);
			nlohmann::get_to_if_present(j, "useUnityHumanoidSplay", settings.useUnityHumanoidSplay);
			nlohmann::get_to_if_present(j, "sendDebugOsc", settings.sendDebugOsc);
			nlohmann::get_to_if_present(j, "alternateCurlTest", settings.alternateCurlTest);
			nlohmann::get_to_if_present(j, "curlDebug", settings.curlDebug);
			nlohmann::get_to_if_present(j, "splayDebug", settings.splayDebug);
		}

		inline void to_json(nlohmann::json& j, const VisualizerSettings& settings)
		{
			j = {{"followLeftHand", settings.followLeftHand},
				 {"followRightHand", settings.followRightHand},
				 {"showBodyTrackingPalmAxes", settings.showBodyTrackingPalmAxes},
				 {"showBodyTrackingJointAxes", settings.showBodyTrackingJointAxes},
				 {"showHandTrackingPalmAxes", settings.showHandTrackingPalmAxes},
				 {"showHandTrackingJointAxes", settings.showHandTrackingJointAxes},
				 {"showBodyTrackerAxes", settings.showBodyTrackerAxes},
				 {"showLookAtModifierCone", settings.showLookAtModifierCone},
				 {"showInFrontModifierCone", settings.showInFrontModifierCone},
				 {"showPalmFacingModifierCone", settings.showPalmFacingModifierCone},
				 {"showControllerPositionTrails", settings.showControllerPositionTrails}};
		}

		inline void from_json(const nlohmann::json& j, VisualizerSettings& settings)
		{
			nlohmann::get_to_if_present(j, "followLeftHand", settings.followLeftHand);
			nlohmann::get_to_if_present(j, "followRightHand", settings.followRightHand);
			nlohmann::get_to_if_present(
				j, "showBodyTrackingPalmAxes", settings.showBodyTrackingPalmAxes);
			nlohmann::get_to_if_present(
				j, "showBodyTrackingJointAxes", settings.showBodyTrackingJointAxes);
			nlohmann::get_to_if_present(
				j, "showHandTrackingPalmAxes", settings.showHandTrackingPalmAxes);
			nlohmann::get_to_if_present(
				j, "showHandTrackingJointAxes", settings.showHandTrackingJointAxes);
			nlohmann::get_to_if_present(j, "showBodyTrackerAxes", settings.showBodyTrackerAxes);
			nlohmann::get_to_if_present(
				j, "showLookAtModifierCone", settings.showLookAtModifierCone);
			nlohmann::get_to_if_present(
				j, "showInFrontModifierCone", settings.showInFrontModifierCone);
			nlohmann::get_to_if_present(
				j, "showPalmFacingModifierCone", settings.showPalmFacingModifierCone);
			nlohmann::get_to_if_present(
				j, "showControllerPositionTrails", settings.showControllerPositionTrails);
		}

		inline void to_json(nlohmann::json& j, const DebugSettings& settings)
		{
			j = {{"rotationFix", settings.rotationFix}, {"rotationOut", settings.rotationOut}};
		}

		inline void from_json(const nlohmann::json& j, DebugSettings& settings)
		{
			nlohmann::get_to_if_present(j, "rotationFix", settings.rotationFix);
			nlohmann::get_to_if_present(j, "rotationOut", settings.rotationOut);
		}

		inline void to_json(nlohmann::json& j, const GestureBinding& binding)
		{
			j = {{"enabled", binding.enabled},
				 {"side", binding.side},
				 {"kind", binding.kind},
				 {"proximityFinger", binding.proximityFinger},
				 {"chainFingers", binding.chainFingers},
				 {"chainLength", binding.chainLength},
				 {"modifiers", binding.modifiers},
				 {"invertedModifiers", binding.invertedModifiers},
				 {"target", binding.target},
				 {"pressAndRelease", binding.pressAndRelease}};
		}

		inline void from_json(const nlohmann::json& j, GestureBinding& binding)
		{
			nlohmann::get_to_if_present(j, "enabled", binding.enabled);
			nlohmann::get_to_if_present(j, "side", binding.side);
			nlohmann::get_to_if_present(j, "kind", binding.kind);
			nlohmann::get_to_if_present(j, "proximityFinger", binding.proximityFinger);
			nlohmann::get_to_if_present(j, "chainFingers", binding.chainFingers);
			nlohmann::get_to_if_present(j, "chainLength", binding.chainLength);
			nlohmann::get_to_if_present(j, "modifiers", binding.modifiers);
			nlohmann::get_to_if_present(j, "invertedModifiers", binding.invertedModifiers);
			nlohmann::get_to_if_present(j, "target", binding.target);
			nlohmann::get_to_if_present(j, "pressAndRelease", binding.pressAndRelease);
		}

		inline void to_json(nlohmann::json& j, const InputSettings& settings)
		{
			j = {{"sendOscInput", settings.sendOscInput},
				 {"joystickReferenceMode", settings.joystickReferenceMode},
				 {"chainGestureTimeoutMS", settings.chainGestureTimeoutMS},
				 {"holdDurationMS", settings.holdDurationMS},
				 {"gateLagTimeMS", settings.gateLagTimeMS},
				 {"pinchDistanceMM", settings.pinchDistanceMM},
				 {"inFrontFovDegrees", settings.inFrontFovDegrees},
				 {"lookAtFovDegrees", settings.lookAtFovDegrees},
				 {"palmFacingFovDegrees", settings.palmFacingFovDegrees},
				 {"gestureBindings", settings.gestureBindings}};
		}

		inline void from_json(const nlohmann::json& j, InputSettings& settings)
		{
			nlohmann::get_to_if_present(j, "sendOscInput", settings.sendOscInput);
			nlohmann::get_to_if_present(
				j, "joystickReferenceMode", settings.joystickReferenceMode);
			nlohmann::get_to_if_present(
				j, "chainGestureTimeoutMS", settings.chainGestureTimeoutMS);
			nlohmann::get_to_if_present(j, "holdDurationMS", settings.holdDurationMS);
			nlohmann::get_to_if_present(j, "gateLagTimeMS", settings.gateLagTimeMS);
			nlohmann::get_to_if_present(j, "pinchDistanceMM", settings.pinchDistanceMM);
			nlohmann::get_to_if_present(j, "inFrontFovDegrees", settings.inFrontFovDegrees);
			nlohmann::get_to_if_present(j, "lookAtFovDegrees", settings.lookAtFovDegrees);
			nlohmann::get_to_if_present(
				j, "palmFacingFovDegrees", settings.palmFacingFovDegrees);
			nlohmann::get_to_if_present(j, "gestureBindings", settings.gestureBindings);
		}

		inline void to_json(nlohmann::json& j, const OpenXRSettings& settings)
		{
			j = {{"runtimeOverridePath", settings.runtimeOverridePath}};
		}

		inline void from_json(const nlohmann::json& j, OpenXRSettings& settings)
		{
			nlohmann::get_to_if_present(j, "runtimeOverridePath", settings.runtimeOverridePath);
		}

		inline void to_json(nlohmann::json& j, const SkeletalInput& settings)
		{
			j = {{"trackingLevel", settings.trackingLevel},
				 {"jointLengthMultiplier", settings.jointLengthMultiplier},
				 {"augmentControllerSkeleton", settings.augmentControllerSkeleton},
				 {"positionOffset", settings.positionOffset},
				 {"orientationOffset", settings.orientationOffset}};
		}

		inline void from_json(const nlohmann::json& j, SkeletalInput& settings)
		{
			nlohmann::get_to_if_present(j, "trackingLevel", settings.trackingLevel);
			nlohmann::get_to_if_present(j, "jointLengthMultiplier", settings.jointLengthMultiplier);
			nlohmann::get_to_if_present(
				j, "augmentControllerSkeleton", settings.augmentControllerSkeleton);
			nlohmann::get_to_if_present(j, "positionOffset", settings.positionOffset);
			nlohmann::get_to_if_present(j, "orientationOffset", settings.orientationOffset);
		}

		inline void to_json(nlohmann::json& j, const SteamVRPoseSmoothingSettings& settings)
		{
			j = {{"steamPoseTimeOffsetMS", settings.steamPoseTimeOffsetMS},
				 {"positionSmoothingMS", settings.positionSmoothingMS},
				 {"rotationSmoothingMS", settings.rotationSmoothingMS},
				 {"linearVelocityMultiplier", settings.linearVelocityMultiplier},
				 {"angularVelocityMultiplier", settings.angularVelocityMultiplier}};
		}

		inline void from_json(const nlohmann::json& j, SteamVRPoseSmoothingSettings& settings)
		{
			nlohmann::get_to_if_present(
				j, "steamPoseTimeOffsetMS", settings.steamPoseTimeOffsetMS);
			nlohmann::get_to_if_present(j, "positionSmoothingMS", settings.positionSmoothingMS);
			nlohmann::get_to_if_present(j, "rotationSmoothingMS", settings.rotationSmoothingMS);
			nlohmann::get_to_if_present(
				j, "linearVelocityMultiplier", settings.linearVelocityMultiplier);
			nlohmann::get_to_if_present(
				j, "angularVelocityMultiplier", settings.angularVelocityMultiplier);
		}

		inline void to_json(nlohmann::json& j, const SteamVRSettings& settings)
		{
			j = {{"autoLaunchApp", settings.autoLaunchApp},
				 {"closeAppOnSteamVRExit", settings.closeAppOnSteamVRExit},
				 {"sendSteamVRInput", settings.sendSteamVRInput},
				 {"transmitLegacyFingerCurl", settings.transmitLegacyFingerCurl},
				 {"blockControllerInputWhileHandTracking",
				  settings.blockControllerInputWhileHandTracking},
				 {"disableOtherControllersWhileHandTracking",
				  settings.disableOtherControllersWhileHandTracking},
				 {"showDevicePoseDiagnostics", settings.showDevicePoseDiagnostics},
				 {"poseSmoothing", settings.poseSmoothing},
				 {"standardPoseSmoothing", settings.standardPoseSmoothing},
				 {"vdxrPoseSmoothing", settings.vdxrPoseSmoothing},
				 {"triggerStabilization", settings.triggerStabilization},
				 {"triggerStabilizationSmoothingMS", settings.triggerStabilizationSmoothingMS},
				 {"triggerStabilizationFalloffMS", settings.triggerStabilizationFalloffMS},
				 {"handTrackingResumeBlendMS", settings.handTrackingResumeBlendMS},
				 {"jitterLastPoseOnTrackingLoss", settings.jitterLastPoseOnTrackingLoss}};
		}

		inline void from_json(const nlohmann::json& j, SteamVRSettings& settings)
		{
			nlohmann::get_to_if_present(j, "autoLaunchApp", settings.autoLaunchApp);
			nlohmann::get_to_if_present(
				j, "closeAppOnSteamVRExit", settings.closeAppOnSteamVRExit);
			nlohmann::get_to_if_present(j, "sendSteamVRInput", settings.sendSteamVRInput);
			nlohmann::get_to_if_present(
				j, "transmitLegacyFingerCurl", settings.transmitLegacyFingerCurl);
			nlohmann::get_to_if_present(j,
										"blockControllerInputWhileHandTracking",
										settings.blockControllerInputWhileHandTracking);
			nlohmann::get_to_if_present(j,
										"disableOtherControllersWhileHandTracking",
										settings.disableOtherControllersWhileHandTracking);
			nlohmann::get_to_if_present(
				j, "showDevicePoseDiagnostics", settings.showDevicePoseDiagnostics);
			nlohmann::get_to_if_present(j, "poseSmoothing", settings.poseSmoothing);
			nlohmann::get_to_if_present(
				j, "standardPoseSmoothing", settings.standardPoseSmoothing);
			nlohmann::get_to_if_present(j, "vdxrPoseSmoothing", settings.vdxrPoseSmoothing);

			nlohmann::get_to_if_present(
				j, "triggerStabilization", settings.triggerStabilization);
			nlohmann::get_to_if_present(
				j, "triggerStabilizationSmoothingMS", settings.triggerStabilizationSmoothingMS);
			nlohmann::get_to_if_present(
				j, "triggerStabilizationFalloffMS", settings.triggerStabilizationFalloffMS);
			nlohmann::get_to_if_present(
				j, "handTrackingResumeBlendMS", settings.handTrackingResumeBlendMS);
			nlohmann::get_to_if_present(
				j, "jitterLastPoseOnTrackingLoss", settings.jitterLastPoseOnTrackingLoss);
		}

		inline void to_json(nlohmann::json& j, const BodyTrackerSettings& settings)
		{
			j = {{"enableBodyTrackers", settings.enableBodyTrackers}, {"enabled", settings.enabled}};
		}

		inline void from_json(const nlohmann::json& j, BodyTrackerSettings& settings)
		{
			nlohmann::get_to_if_present(j, "enableBodyTrackers", settings.enableBodyTrackers);
			nlohmann::get_to_if_present(j, "enabled", settings.enabled);
		}

		inline void to_json(nlohmann::json& j, const ControllerButtonOverride& override)
		{
			j = {{"buttonPath", override.buttonPath}, {"suppressTouch", override.suppressTouch}};
		}

		inline void from_json(const nlohmann::json& j, ControllerButtonOverride& override)
		{
			nlohmann::get_to_if_present(j, "buttonPath", override.buttonPath);
			nlohmann::get_to_if_present(j, "suppressTouch", override.suppressTouch);
		}

		inline void to_json(nlohmann::json& j, const DeviceConfig& config)
		{
			j = {{"serial", config.serial},
				 {"role", config.role},
				 {"actAsTracker", config.actAsTracker},
				 {"alsoWhenHeld", config.alsoWhenHeld},
				 {"inputOverrides", config.inputOverrides}};
		}

		inline void from_json(const nlohmann::json& j, DeviceConfig& config)
		{
			nlohmann::get_to_if_present(j, "serial", config.serial);
			nlohmann::get_to_if_present(j, "role", config.role);
			nlohmann::get_to_if_present(j, "actAsTracker", config.actAsTracker);
			nlohmann::get_to_if_present(j, "alsoWhenHeld", config.alsoWhenHeld);
			nlohmann::get_to_if_present(j, "inputOverrides", config.inputOverrides);
		}

		inline void to_json(nlohmann::json& j, const DeviceSettings& settings)
		{
			j = {{"preferredLeftControllerSerial", settings.preferredLeftControllerSerial},
				 {"preferredRightControllerSerial", settings.preferredRightControllerSerial},
				 {"devices", settings.devices}};
		}

		inline void from_json(const nlohmann::json& j, DeviceSettings& settings)
		{
			nlohmann::get_to_if_present(
				j, "preferredLeftControllerSerial", settings.preferredLeftControllerSerial);
			nlohmann::get_to_if_present(
				j, "preferredRightControllerSerial", settings.preferredRightControllerSerial);
			nlohmann::get_to_if_present(j, "devices", settings.devices);
		}

		inline void to_json(nlohmann::json& j, const TrackingFeaturesSettings& settings)
		{
			j = {{"enableUpperBodyTracking", settings.enableUpperBodyTracking},
				 {"enableSimultaneousTracking", settings.enableSimultaneousTracking},
				 {"forceMultimodalHandPrimary", settings.forceMultimodalHandPrimary}};
		}

		inline void from_json(const nlohmann::json& j, TrackingFeaturesSettings& settings)
		{
			nlohmann::get_to_if_present(
				j, "enableUpperBodyTracking", settings.enableUpperBodyTracking);
			nlohmann::get_to_if_present(
				j, "enableSimultaneousTracking", settings.enableSimultaneousTracking);
			nlohmann::get_to_if_present(
				j, "forceMultimodalHandPrimary", settings.forceMultimodalHandPrimary);
		}

		inline void to_json(nlohmann::json& j, const HandOfLesserSettings& settings)
		{
			j = {{"version", settings.version},
				 {"general", settings.general},
				 {"fingerBend", settings.fingerBend},
				 {"skeletalBend", settings.skeletalBend},
				 {"handPose", settings.handPose},
				 {"openxr", settings.openxr},
				 {"vrchat", settings.vrchat},
				 {"debug", settings.debug},
				 {"steamvr", settings.steamvr},
				 {"visualizer", settings.visualizer},
				 {"input", settings.input},
				 {"skeletal", settings.skeletal},
				 {"bodyTrackers", settings.bodyTrackers},
				 {"deviceSettings", settings.deviceSettings},
				 {"trackingFeatures", settings.trackingFeatures}};
		}

		inline void from_json(const nlohmann::json& j, HandOfLesserSettings& settings)
		{
			nlohmann::get_to_if_present(j, "version", settings.version);
			nlohmann::get_to_if_present(j, "general", settings.general);
			nlohmann::get_to_if_present(j, "fingerBend", settings.fingerBend);
			nlohmann::get_to_if_present(j, "skeletalBend", settings.skeletalBend);
			nlohmann::get_to_if_present(j, "handPose", settings.handPose);
			nlohmann::get_to_if_present(j, "openxr", settings.openxr);
			nlohmann::get_to_if_present(j, "vrchat", settings.vrchat);
			nlohmann::get_to_if_present(j, "debug", settings.debug);
			nlohmann::get_to_if_present(j, "steamvr", settings.steamvr);
			nlohmann::get_to_if_present(j, "visualizer", settings.visualizer);
			nlohmann::get_to_if_present(j, "input", settings.input);
			nlohmann::get_to_if_present(j, "skeletal", settings.skeletal);
			nlohmann::get_to_if_present(j, "bodyTrackers", settings.bodyTrackers);
			nlohmann::get_to_if_present(j, "deviceSettings", settings.deviceSettings);
			nlohmann::get_to_if_present(j, "trackingFeatures", settings.trackingFeatures);
		}
	} // namespace settings
} // namespace HOL
