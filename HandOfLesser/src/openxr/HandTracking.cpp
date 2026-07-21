#include "HandTracking.h"
#include "HandTrackingInterface.h"
#include <HandOfLesserCommon.h>
#include <algorithm>
#include <iterator>
#include <iostream>
#include "src/core/ui/user_interface.h"
#include "XrUtils.h"
#include "src/core/settings_global.h"
#include "xr_joint_utils.h"
#include "src/hands/gesture_binding_builder.h"
#include "src/core/state_global.h"
#include "src/steamvr/steamvr_input.h"

using namespace HOL;
using namespace HOL::OpenXR;
using namespace std::chrono_literals;

void HandTracking::init(xr::UniqueDynamicInstance& instance, xr::UniqueDynamicSession& session)
{
	HandTrackingInterface::init(instance);
	this->initHands(session);
	rebuildActions();
}

void HandTracking::initHands(xr::UniqueDynamicSession& session)
{
	this->mLeftHand.init(session, HOL::LeftHand);
	this->mRightHand.init(session, HOL::RightHand);
}

void HOL::OpenXR::HandTracking::rebuildActions()
{
	auto newActionSet = std::make_shared<ActionSet>();
	newActionSet->actionsByBindingIndex.resize(Config.input.gestureBindings.size());

	for (size_t i = 0; i < Config.input.gestureBindings.size(); i++)
	{
		const auto& binding = Config.input.gestureBindings[i];

		// Skip system aim bindings when the runtime doesn't support it
		if (binding.kind == settings::GestureKind::SystemAim
			&& !HOL::state::Runtime.supportsHandTrackingAim)
		{
			continue;
		}

		auto action = GestureBindings::buildAction(binding);
		if (action)
		{
			newActionSet->actions.push_back(action);
			newActionSet->actionsByBindingIndex[i] = action;
			newActionSet->bindingIndicesByTarget[static_cast<size_t>(binding.target)].push_back(i);
		}
	}

	this->mActionSet.store(std::shared_ptr<const ActionSet>(newActionSet), std::memory_order_release);
}

std::shared_ptr<BaseAction> HOL::OpenXR::HandTracking::getActionForBindingIndex(size_t bindingIndex) const
{
	std::shared_ptr<const ActionSet> actionSet = this->mActionSet.load(std::memory_order_acquire);
	if (!actionSet || bindingIndex >= actionSet->actionsByBindingIndex.size())
	{
		return nullptr;
	}

	return actionSet->actionsByBindingIndex[bindingIndex];
}

void HandTracking::updateHands(xr::UniqueDynamicSpace& space,
						   XrTime time,
						   OpenXRBody& bodyTracker,
						   const HOL::PoseLocation* hmdPose,
						   bool skeletalUpdate)
{
	auto now = std::chrono::steady_clock::now();
	const HOL::HandTrackingSample* leftSample = nullptr;
	const HOL::HandTrackingSample* rightSample = nullptr;
	if (HOL::state::Runtime.isSteamVR)
	{
		// Querying OpenXR here would feed our emulated hands back into themselves. Reconstruct the
		// hand from full-skeleton controllers observed by the driver instead.
		leftSample = mSteamVRHandTrackingSource.getSample(
			HOL::LeftHand, hmdPose, Config.handPose.applyBaseOffset, skeletalUpdate);
		rightSample = mSteamVRHandTrackingSource.getSample(
			HOL::RightHand, hmdPose, Config.handPose.applyBaseOffset, skeletalUpdate);
	}
	this->mLeftHand.updateJointLocations(
		space,
		time,
		bodyTracker,
		getTriggerStabilizationSmoothingMS(HOL::LeftHand, now),
		skeletalUpdate,
		leftSample);
	this->mRightHand.updateJointLocations(
		space,
		time,
		bodyTracker,
		getTriggerStabilizationSmoothingMS(HOL::RightHand, now),
		skeletalUpdate,
		rightSample);

	if (!skeletalUpdate)
	{
		return;
	}

	// Populate gesture data
	HOL::Gesture::GestureData data;
	data.bodyJoints = bodyTracker.getLastJointLocations();
	data.ReferenceOrientation = bodyTracker.getReferenceOrientation(
		Config.input.joystickReferenceMode, data.ReferenceOrientationValid);

	for (int i = 0; i < HandSide::HandSide_MAX; i++)
	{
		OpenXRHand* hand = getHand((HandSide)i);
		data.handPose[i] = &hand->handPose;
		data.joints[i] = hand->getLastJointLocations();
		data.aimState[i] = hand->getAimState();
	}

	// Evaluate gestures
	std::shared_ptr<const ActionSet> actionSet = this->mActionSet.load(std::memory_order_acquire);
	if (!actionSet)
	{
		return;
	}

	for (auto& action : actionSet->actions)
	{
		action->evaluate(data);
	}

	updateTriggerStabilizationState(*actionSet);
}

void HOL::OpenXR::HandTracking::updateTriggerStabilizationState(const ActionSet& actionSet)
{
	this->mTriggerStabilizationHeld.fill(false);
	if (!Config.steamvr.triggerStabilization)
	{
		return;
	}

	for (size_t bindingIndex :
		 actionSet.getBindingIndicesForTarget(settings::InputTarget::Trigger))
	{
		if (bindingIndex >= Config.input.gestureBindings.size()
			|| bindingIndex >= actionSet.actionsByBindingIndex.size())
		{
			continue;
		}

		const auto& binding = Config.input.gestureBindings[bindingIndex];
		const auto& action = actionSet.actionsByBindingIndex[bindingIndex];
		if (!action)
		{
			continue;
		}

		if (binding.side >= 0 && binding.side < HOL::HandSide_MAX
			&& action->getActionData().isDown)
		{
			this->mTriggerStabilizationHeld[binding.side] = true;
		}

		if (!action->getActionData().onDown)
		{
			continue;
		}

		if (binding.side >= 0 && binding.side < HOL::HandSide_MAX)
		{
			this->mLastTriggerStabilizationTime[binding.side] = std::chrono::steady_clock::now();
		}
	}
}

float HOL::OpenXR::HandTracking::getTriggerStabilizationSmoothingMS(
	HOL::HandSide side, std::chrono::steady_clock::time_point now) const
{
	if (!Config.steamvr.triggerStabilization)
	{
		return 0.0f;
	}

	if (!this->mTriggerStabilizationHeld[side])
	{
		return 0.0f;
	}

	float falloffMS = Config.steamvr.triggerStabilizationFalloffMS;
	if (falloffMS <= 0.0f)
	{
		return 0.0f;
	}

	const auto& triggerTime = this->mLastTriggerStabilizationTime[side];
	if (triggerTime == std::chrono::steady_clock::time_point{})
	{
		return 0.0f;
	}

	float elapsedMS
		= std::chrono::duration<float, std::milli>(now - triggerTime).count();
	if (elapsedMS >= falloffMS)
	{
		return 0.0f;
	}

	float remainingAlpha = 1.0f - (elapsedMS / falloffMS);
	return Config.steamvr.triggerStabilizationSmoothingMS * std::clamp(remainingAlpha, 0.0f, 1.0f);
}

void HandTracking::updateInputs()
{
	submitLegacyFingerCurl();
}

void HOL::OpenXR::HandTracking::submitLegacyFingerCurl()
{
	const bool isEmulatedIndex
		= Config.handPose.controllerMode == ControllerMode::EmulateControllerMode
		  && Config.handPose.emulatedControllerProfile
				 == EmulatedControllerProfile::EmulatedControllerProfile_Index;

	for (int i = 0; i < HandSide::HandSide_MAX; i++)
	{
		OpenXRHand* hand = getHand((HandSide)i);
		if (!hand->handPose.poseValid)
		{
			continue;
		}

		const float fingerCurlIndex
			= mapCurlToSteamVR(hand->handPose.fingers[FingerType::FingerIndex].getCurlSum());
		const float fingerCurlMiddle
			= mapCurlToSteamVR(hand->handPose.fingers[FingerType::FingerMiddle].getCurlSum());
		const float fingerCurlRing
			= mapCurlToSteamVR(hand->handPose.fingers[FingerType::FingerRing].getCurlSum());
		const float fingerCurlPinky
			= mapCurlToSteamVR(hand->handPose.fingers[FingerType::FingerLittle].getCurlSum());
		const float averageFingerCurl
			= (fingerCurlIndex + fingerCurlMiddle + fingerCurlRing + fingerCurlPinky) / 4.0f;
		const float gripForce = std::clamp((averageFingerCurl - 0.5f) / 0.5f, 0.0f, 1.0f);

		if (!isEmulatedIndex)
		{
			continue;
		}

		SteamVR::SteamVRInput::Current->submitFloat(
			(HandSide)i, SteamVR::Input::Grip.force(), gripForce);

		if (!Config.steamvr.transmitLegacyFingerCurl)
		{
			continue;
		}

		SteamVR::SteamVRInput::Current->submitFloat(
			(HandSide)i, SteamVR::Input::Finger.index(), fingerCurlIndex);
		SteamVR::SteamVRInput::Current->submitFloat(
			(HandSide)i, SteamVR::Input::Finger.middle(), fingerCurlMiddle);
		SteamVR::SteamVRInput::Current->submitFloat(
			(HandSide)i, SteamVR::Input::Finger.ring(), fingerCurlRing);
		SteamVR::SteamVRInput::Current->submitFloat(
			(HandSide)i, SteamVR::Input::Finger.pinky(), fingerCurlPinky);
	}
}

OpenXRHand* HandTracking::getHand(HOL::HandSide side)
{
	if (side == HOL::HandSide::LeftHand)
	{
		return &this->mLeftHand;
	}
	else
	{
		return &this->mRightHand;
	}
}

HOL::HandTransformPayload HandTracking::getTransformPayload(HOL::HandSide side)
{
	OpenXRHand* hand = getHand(side);

	HOL::HandTransformPayload payload;

	payload.active = hand->handPose.active;
	payload.valid = hand->handPose.poseValid;
	payload.tracked = hand->handPose.poseTracked;
	payload.stale = hand->handPose.poseStale;
	payload.side = (HOL::HandSide)side;
	payload.location = hand->handPose.palmLocation;
	payload.velocity = hand->handPose.palmVelocity;

	if (HOL::state::Runtime.isSteamVR)
	{
		mSteamVRHandTrackingSource.applySourcePose(side, payload);
	}

	return payload;
}

HOL::HandPose& HandTracking::getHandPose(HOL::HandSide side)
{
	OpenXRHand& hand = (side == HOL::LeftHand) ? this->mLeftHand : this->mRightHand;
	return hand.handPose;
}

void HandTracking::updateSteamVRHandBaseline(const HOL::SteamVRHandBaselinePayload& payload)
{
	mSteamVRHandTrackingSource.updateBaseline(payload);
}

void HandTracking::updateSteamVRHandPose(const HOL::SteamVRHandPosePayload& payload)
{
	mSteamVRHandTrackingSource.updatePose(payload);
}

void HOL::OpenXR::HandTracking::drawHands()
{
	auto vis = HOL::UserInterface::Current->getVisualizer();
	if (!vis->isActive())
	{
		return;
	}

	auto colorGrey = IM_COL32(155, 155, 155, 255);
	auto colorWhite = IM_COL32(255, 255, 255, 255);

	for (int i = 0; i < HandSide::HandSide_MAX; i++)
	{
		XrHandJointLocationEXT* jointLocations = this->getHand((HandSide)i)->getLastJointLocations();

		for (int j = 0; j < XR_HAND_JOINT_COUNT_EXT; j++)
		{
			XrHandJointLocationEXT& joint = jointLocations[j];

			// WIll replace this later anyway so nevermind wasteful conversion
			vis->submitPoint(OpenXR::toEigenVector(joint.pose.position), colorGrey, 5);
		}

		// Also draw some white skeleton lines
		{
			for (int finger = 0; finger < FingerType_MAX; finger++)
			{
				XrHandJointEXT rootJoint = OpenXR::getRootJoint((FingerType)finger);
				for (int j = 0; j < 4; j++)
				{
					XrHandJointLocationEXT& joint = jointLocations[rootJoint + j];
					XrHandJointLocationEXT& nextJoint = jointLocations[rootJoint + j + 1];
					vis->submitLine(OpenXR::toEigenVector(joint.pose.position),
									OpenXR::toEigenVector(nextJoint.pose.position),
									colorWhite,
									2);
				}
			}
		}

		XrHandJointLocationEXT& palm = jointLocations[XR_HAND_JOINT_PALM_EXT];

		// Visualize palm orientation axes if enabled
		if (Config.visualizer.showHandTrackingPalmAxes)
		{
			if (palm.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT)
			{
				vis->submitOrientationAxes(OpenXR::toEigenVector(palm.pose.position),
										   OpenXR::toEigenQuaternion(palm.pose.orientation),
										   0.120f,
										   6.0f);
			}
		}

		// Display the raw OpenXR hand-joint orientations directly so runtime-specific issues can
		// be inspected without any downstream skeletal/OSC processing in the way.
		if (Config.visualizer.showHandTrackingJointAxes)
		{
			for (int j = 0; j < XR_HAND_JOINT_COUNT_EXT; j++)
			{
				XrHandJointLocationEXT& joint = jointLocations[j];
				if (!(joint.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)
					|| !(joint.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
				{
					continue;
				}

				vis->submitOrientationAxes(OpenXR::toEigenVector(joint.pose.position),
										   OpenXR::toEigenQuaternion(joint.pose.orientation),
										   0.040f,
										   2.0f);
			}
		}

		// This doesn't super go here but it's a good place for it.
		if (i == HandSide::LeftHand && HOL::Config.visualizer.followLeftHand)
		{
			vis->centerTo(OpenXR::toEigenVector(palm.pose.position));
		}
		else if (i == HandSide::RightHand && HOL::Config.visualizer.followRightHand)
		{
			vis->centerTo(OpenXR::toEigenVector(palm.pose.position));
		}
	}
}
