#pragma once

#include "controller_common.h"
#include "src/core/hand_of_lesser.h"
#include <chrono>
#include <driverlog.h>

namespace HOL::ControllerCommon
{
	static std::default_random_engine JitterGenerator = std::default_random_engine(0);
	static std::uniform_real_distribution<float> JitterDistribution
		= std::uniform_real_distribution<float>(0, 0.0001);

	vr::DriverPose_t generatePose(HOL::HandTransformPayload* payload, bool deviceConnected)
	{
		vr::DriverPose_t pose = {0};
		const HOL::PoseLocation driverFromHead = HOL::getControllerPoseOffset(
			payload->side,
			HandOfLesser::Config.handPose.applyBaseOffset,
			HandOfLesser::Config.handPose.positionOffset,
			HandOfLesser::Config.handPose.orientationOffset);

		if (payload->hasSteamVRSourcePose)
		{
			// Preserve the source driver's coordinate and prediction structure, but move its
			// effective device pose to the palm transform produced by app-side processing.
			pose = payload->steamVRSourcePose;
			HOL::PoseLocation devicePose;
			devicePose.position = payload->location.position
				+ payload->location.orientation * driverFromHead.position;
			devicePose.orientation
				= payload->location.orientation * driverFromHead.orientation;
			HOL::SteamVR::setSteamVRDevicePose(pose, devicePose);
			HOL::SteamVR::setSteamVRVelocityAtPosition(
				pose, payload->location.position, payload->velocity);
		}
		else
		{
			pose.qWorldFromDriverRotation.w = 1.f;
			pose.qDriverFromHeadRotation.w = driverFromHead.orientation.w();
			pose.qDriverFromHeadRotation.x = driverFromHead.orientation.x();
			pose.qDriverFromHeadRotation.y = driverFromHead.orientation.y();
			pose.qDriverFromHeadRotation.z = driverFromHead.orientation.z();
			pose.vecDriverFromHeadTranslation[0] = driverFromHead.position.x();
			pose.vecDriverFromHeadTranslation[1] = driverFromHead.position.y();
			pose.vecDriverFromHeadTranslation[2] = driverFromHead.position.z();

			pose.vecPosition[0] = payload->location.position.x();
			pose.vecPosition[1] = payload->location.position.y();
			pose.vecPosition[2] = payload->location.position.z();
			pose.qRotation.w = payload->location.orientation.w();
			pose.qRotation.x = payload->location.orientation.x();
			pose.qRotation.y = payload->location.orientation.y();
			pose.qRotation.z = payload->location.orientation.z();
			pose.vecVelocity[0] = payload->velocity.linearVelocity.x();
			pose.vecVelocity[1] = payload->velocity.linearVelocity.y();
			pose.vecVelocity[2] = payload->velocity.linearVelocity.z();
			pose.vecAngularVelocity[0] = payload->velocity.angularVelocity.x();
			pose.vecAngularVelocity[1] = payload->velocity.angularVelocity.y();
			pose.vecAngularVelocity[2] = payload->velocity.angularVelocity.z();
		}

		pose.poseTimeOffset
			= HandOfLesser::Config.steamvr.poseSmoothing.steamPoseTimeOffsetMS / 1000.0f;

		// The pose we provided is valid.
		// This should be set is
		pose.poseIsValid = true;

		// Our device is always connected.
		// In reality with physical devices, when they get disconnected,
		// set this to false and icons in SteamVR will be updated to show the device is disconnected
		pose.deviceIsConnected = true;

		// The state of our tracking. For our virtual device, it's always going to be ok,
		// but this can get set differently to inform the runtime about the state of the device's
		// tracking and update the icons to inform the user accordingly.
		pose.result // What state to use for disconnected?
			= deviceConnected ? vr::TrackingResult_Running_OK : vr::TrackingResult_Uninitialized;

		return pose;
	}

	vr::DriverPose_t generateDisconnectedPose()
	{
		// Tells SteamVR that the controller is disconnected
		vr::DriverPose_t pose = {0};

		pose.deviceIsConnected = false;
		pose.poseIsValid = false;

		return pose;
	}

	vr::DriverPose_t addJitter(const vr::DriverPose_t& existingPose)
	{
		// VRChat is stupid and ignores all status values passed to it by SteamVR
		// in favor is checking whether position values change. If they remain completely
		// static for a short period of time it decides tracking has been lost and moves
		// your arms to the sides. Idiots.
		vr::DriverPose_t jitteredPose = existingPose;

		jitteredPose.vecPosition[0] += JitterDistribution(JitterGenerator);
		jitteredPose.vecPosition[1] += JitterDistribution(JitterGenerator);
		jitteredPose.vecPosition[2] += JitterDistribution(JitterGenerator);

		return jitteredPose;
	}

	void offsetPose(vr::DriverPose_t& existingPose,
					HOL::HandSide side,
					Eigen::Vector3f translationOffset,
					Eigen::Vector3f rotationOffset)
	{

		if (side == HandSide::LeftHand)
		{
			// Base offsets are for the left hand
		}
		else
		{
			rotationOffset = flipHandRotation(rotationOffset);
			translationOffset = flipHandTranslation(translationOffset);
		}

		/////////////////////
		// Existing values
		////////////////////

		Eigen::Quaternionf poseRotation = Eigen::Quaternionf(existingPose.qRotation.w,
															 existingPose.qRotation.x,
															 existingPose.qRotation.y,
															 existingPose.qRotation.z);

		Eigen::Vector3f poseTranslation = Eigen::Vector3f(
			existingPose.vecPosition[0], existingPose.vecPosition[1], existingPose.vecPosition[2]);

		///////////////////////////
		// Apply driver offsets
		///////////////////////////

		// Native controller poses often keep part of their transform in DriverFromHead.
		// SteamVR can use that original structure for its own prediction, so in fallback-only
		// mode we must not bake it into vecPosition/qRotation and clear it out.
		// Instead, convert to the final native pose, apply our calibration offset there,
		// then solve back to the local pose while preserving DriverFromHead unchanged.

		Eigen::Vector3f vecDriverFromHead
			= Eigen::Vector3f(existingPose.vecDriverFromHeadTranslation[0],
							  existingPose.vecDriverFromHeadTranslation[1],
							  existingPose.vecDriverFromHeadTranslation[2]);
		Eigen::Quaternionf qDriverFromHead
			= Eigen::Quaternionf(existingPose.qDriverFromHeadRotation.w,
								 existingPose.qDriverFromHeadRotation.x,
								 existingPose.qDriverFromHeadRotation.y,
								 existingPose.qDriverFromHeadRotation.z);

		poseTranslation = HOL::translateLocal(poseTranslation, poseRotation, vecDriverFromHead);
		poseRotation = poseRotation * qDriverFromHead;

		////////////////////
		// Apply our offsets
		////////////////////

		// Offsets are expected to be applied translation, then rotation.
		// We should try to adhere to this with all our offsets too.
		poseTranslation = HOL::translateLocal(poseTranslation, poseRotation, translationOffset);
		poseRotation
			= HOL::rotateLocal(poseRotation, HOL::quaternionFromEulerAnglesDegrees(rotationOffset));

		//////////////////////////////////////
		// Restore the original pose structure
		//////////////////////////////////////

		// Leave the native DriverFromHead data and native velocities untouched so SteamVR can
		// keep predicting this as the original tracked controller instead of as a rebased pose.
		Eigen::Quaternionf localPoseRotation = poseRotation * qDriverFromHead.inverse();
		Eigen::Vector3f localPoseTranslation
			= poseTranslation - (localPoseRotation * vecDriverFromHead);

		//////////
		// Assign
		//////////

		existingPose.vecPosition[0] = localPoseTranslation.x();
		existingPose.vecPosition[1] = localPoseTranslation.y();
		existingPose.vecPosition[2] = localPoseTranslation.z();

		existingPose.qRotation.w = localPoseRotation.w();
		existingPose.qRotation.x = localPoseRotation.x();
		existingPose.qRotation.y = localPoseRotation.y();
		existingPose.qRotation.z = localPoseRotation.z();
	}

	void applyDriverOffset(Eigen::Vector3f& posePosition,
						   Eigen::Quaternionf& poseRotation,
						   const vr::DriverPose_t& pose)
	{
		Eigen::Vector3f vecDriverFromHead = Eigen::Vector3f(pose.vecDriverFromHeadTranslation[0],
															pose.vecDriverFromHeadTranslation[1],
															pose.vecDriverFromHeadTranslation[2]);
		Eigen::Quaternionf qDriverFromHead = Eigen::Quaternionf(pose.qDriverFromHeadRotation.w,
																pose.qDriverFromHeadRotation.x,
																pose.qDriverFromHeadRotation.y,
																pose.qDriverFromHeadRotation.z);

		posePosition = HOL::translateLocal(posePosition, poseRotation, vecDriverFromHead);
		poseRotation = poseRotation * qDriverFromHead;
	}

} // namespace HOL::ControllerCommon
