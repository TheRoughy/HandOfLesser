#include "openxr_hand.h"
#include "XrUtils.h"
#include "xr_joint_utils.h"
#include <HandOfLesserCommon.h>
#include "HandTrackingInterface.h"
#include "src/core/settings_global.h"
#include "src/core/state_global.h"
#include "src/core/ui/display_global.h"
#include <cmath>
#include <iostream>
#include <utility>

#include <Eigen/Core>
#include <Eigen/Geometry>

using namespace HOL;
using namespace HOL::OpenXR;

namespace
{
	float getSmoothingAlpha(float smoothingMS,
							bool hasPreviousSample,
							bool poseStateChanged,
							XrTime previousSampleTime,
							XrTime currentSampleTime)
	{
		if (!hasPreviousSample || poseStateChanged || previousSampleTime <= 0
			|| currentSampleTime <= previousSampleTime)
		{
			return 1.0f;
		}

		float smoothingTimeSeconds = smoothingMS / 1000.0f;
		if (smoothingTimeSeconds <= 0.0f)
		{
			return 1.0f;
		}

		float sampleDeltaSeconds
			= (float)(currentSampleTime - previousSampleTime) / 1000000000.0f;
		if (sampleDeltaSeconds >= 0.25f)
		{
			// A long gap means the old filtered state is no longer representative.
			return 1.0f;
		}

		return 1.0f - std::exp(-sampleDeltaSeconds / smoothingTimeSeconds);
	}

	void applyOculusThumbOrientationFix(XrHandJointLocationEXT jointLocations[], HandSide side)
	{
		constexpr XrHandJointEXT ThumbJoints[] = {
			XR_HAND_JOINT_THUMB_METACARPAL_EXT,
			XR_HAND_JOINT_THUMB_PROXIMAL_EXT,
			XR_HAND_JOINT_THUMB_DISTAL_EXT,
			XR_HAND_JOINT_THUMB_TIP_EXT,
		};

		Eigen::Quaternionf thumbRotationFix
			= HOL::quaternionFromEulerAnglesDegrees(0.0f, 0.0f, side == HandSide::LeftHand ? - 90.0f : 90.0f);

		for (XrHandJointEXT joint : ThumbJoints)
		{
			XrHandJointLocationEXT& location = jointLocations[joint];
			if (!(location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
			{
				continue;
			}

			Eigen::Quaternionf orientation = HOL::OpenXR::toEigenQuaternion(location.pose.orientation);
			orientation = HOL::rotateLocal(orientation, thumbRotationFix);
			location.pose.orientation.w = orientation.w();
			location.pose.orientation.x = orientation.x();
			location.pose.orientation.y = orientation.y();
			location.pose.orientation.z = orientation.z();
		}
	}
} // namespace

OpenXRHand::~OpenXRHand()
{
	this->shutdown();
}

void OpenXRHand::shutdown()
{
	HandTrackingInterface::destroyHandTracker(this->mHandTracker);
}

void OpenXRHand::init(xr::UniqueDynamicSession& session, HOL::HandSide side)
{
	this->mSide = side;
	bool requestUnobstructedDataSource = HOL::state::Runtime.supportsHandTrackingDataSource;
	HandTrackingInterface::createHandTracker(
		session, toOpenXRHandSide(side), this->mHandTracker, requestUnobstructedDataSource);
}

XrHandJointLocationEXT* OpenXRHand::getLastJointLocations()
{
	return this->mJointLocations;
}

const XrHandTrackingAimStateFB* OpenXRHand::getAimState() const
{
	return &this->mAimState;
}

void OpenXRHand::calculateCurlSplay()
{
	if (!this->handPose.poseTracked)
	{
		// Leave previous pose if hand not tracked
		return;
	}

	const auto getJointOrientation = [&](XrHandJointEXT joint)
	{ return toEigenQuaternion(this->mJointLocations[joint].pose.orientation); };

	const auto getJointPosition = [&](XrHandJointEXT joint)
	{ return toEigenVector(this->mJointLocations[joint].pose.position); };

	// Get the orientation of the 4 relevant joints for each finger
	// the thumb has a special case
	const auto getRawOrientation = [&](FingerType finger, Eigen::Quaternionf rawOrientationOut[])
	{
		XrHandJointEXT rootJoint = OpenXR::getRootJoint(finger); // METACARPAL or wrist ( thumb )

		// openxr joints are hierarchical, so we can +1 until we reach the tip of the finger
		for (int i = 0; i < 4; i++)
		{
			rawOrientationOut[i] = getJointOrientation((XrHandJointEXT)(rootJoint + i));
		}

		// The root bone for the thumb would be the bone before the metacarpal,
		// which doesn't exist; the next bone is the wrist.
		// Use wrist orientation, but offset by user input to calibrate correctly.
		if (finger == FingerType::FingerThumb)
		{
			Eigen::Quaternionf wristOrientation = getJointOrientation(rootJoint);
			Eigen::Vector3f rawOffset = Config.fingerBend.ThumbAxisOffset;
			if (this->mSide == HandSide::RightHand)
			{
				// Input values are for left hand
				rawOffset = flipHandRotation(rawOffset);
			}

			// Z axis is probably all you want to adjust
			Eigen::Quaternionf baseOffset = HOL::quaternionFromEulerAnglesDegrees(
				rawOffset.x(), rawOffset.y(), rawOffset.z());

			rawOrientationOut[0] = wristOrientation * (baseOffset);
		}
		else
		{
			// For the sake of the humanoid rig, using the palm as a reference is better
			rawOrientationOut[0] = getJointOrientation(XrHandJointEXT::XR_HAND_JOINT_PALM_EXT);
		}
	};

	// Reuse array to store raw orientations of joints
	Eigen::Quaternionf rawOrientation[4];

	// For each finger
	for (int i = 0; i < FingerType::FingerType_MAX; i++)
	{
		FingerBend* bend = &this->handPose.fingers[i];
		FingerType finger = (FingerType)i;
		getRawOrientation(finger, rawOrientation);

		// For each joint + next joint
		for (int j = 0; j < 3; j++)
		{
			// Pass current joint, and the next joint, starting from METACARPAL.
			bend->bend[j] = computeCurl(rawOrientation[j], rawOrientation[j + 1]);

			// Curling inwards is negative, outwards positive, with 0 being a straight finger.
			// this is a bit unintuitive, so let's flip it.
			bend->bend[j] *= -1.0f;
		}

		// Between metacarpal and proximal. No flipping this.
		//bend->bend[FingerBendType::Splay] = computeSplay(rawOrientation[0], rawOrientation[1]);
		bend->bend[FingerBendType::Splay]
			= computeUncurledSplay(rawOrientation[0], rawOrientation[1], -bend->bend[0]);	// Note that we flip it again

		if (Config.vrchat.useUnityHumanoidSplay)
		{
			// Special values for unity's broken humanoid rig
			// Only splay is different
			Eigen::Quaternionf palmRot
				= getJointOrientation(XrHandJointEXT::XR_HAND_JOINT_PALM_EXT);

			if (finger == FingerType::FingerThumb)
			{
				palmRot = rawOrientation[0];
			}

			Eigen::Vector3f knucklePos = getJointPosition(OpenXR::getFirstFingerJoint(finger));
			Eigen::Vector3f splayRefPos
				= getJointPosition((XrHandJointEXT)(OpenXR::getFirstFingerJoint(finger) + 1));

			bend->setSplay(computeHumanoidSplay(palmRot, knucklePos, splayRefPos));
		}
		else
		{
			// URGHHHHHHH




		}
	}
}

void OpenXRHand::updateJointLocations(xr::UniqueDynamicSpace& space,
									  XrTime time,
									  OpenXRBody& bodyTracker,
									  float triggerStabilizationSmoothingMS,
									  const HOL::HandTrackingSample* externalSample)
{
	// Copy to prev
	std::copy(
		std::begin(mJointLocations), std::end(mJointLocations), std::begin(mPrevJointLocations));
	bool prevActive = mPrevActive;
	bool prevPoseValid = mPrevPoseValid;
	bool prevPoseTracked = mPrevPoseTracked;

	bool handActive = false;
	const bool usingExternalSample
		= externalSample != nullptr && externalSample->hasUpdateGeneration;
	bool useHandTrackingDataSource = HOL::state::Runtime.supportsHandTrackingDataSource
		&& externalSample == nullptr;
	this->mAimState = {XR_TYPE_HAND_TRACKING_AIM_STATE_FB};
	this->mDataSourceState = {XR_TYPE_HAND_TRACKING_DATA_SOURCE_STATE_EXT};
	XrResult result = XR_SUCCESS;
	if (externalSample != nullptr)
	{
		std::copy(std::begin(externalSample->joints),
				  std::end(externalSample->joints),
				  std::begin(mJointLocations));
		std::copy(std::begin(externalSample->velocities),
				  std::end(externalSample->velocities),
				  std::begin(mJointVelocities));
		mAimState = externalSample->aimState;
		mDataSourceState = externalSample->dataSourceState;
		handActive = externalSample->active;
	}
	else
	{
		result = HandTrackingInterface::locateHandJoints(
			this->mHandTracker,
			space,
			time,
			this->mJointLocations,
			this->mJointVelocities,
			handActive,
			HOL::state::Runtime.supportsHandTrackingAim ? &this->mAimState : nullptr,
			useHandTrackingDataSource ? &this->mDataSourceState : nullptr);
	}
	if (result != XR_SUCCESS)
	{
		this->mLastPoseUpdateTime = {};
		HOL::display::HandTransform[this->mSide].updateRateMS.store(0.0f);
		this->handPose = {};
		this->handPose.poseStale = !prevActive && !prevPoseValid && !prevPoseTracked;
		this->mHasPrevRawPose = false;
		this->mHasFilteredPalmPose = false;
		this->mPrevExternalUpdateGeneration = 0;
		this->mPrevFilteredSampleTime = 0;
		this->mPrevActive = this->handPose.active;
		this->mPrevPoseValid = this->handPose.poseValid;
		this->mPrevPoseTracked = this->handPose.poseTracked;
		HOL::display::HandTransform[this->mSide].trackedJointCount = 0;
		HOL::display::HandTransform[this->mSide].active = false;
		HOL::display::HandTransform[this->mSide].positionValid = false;
		HOL::display::HandTransform[this->mSide].positionTracked = false;
		HOL::display::HandTransform[this->mSide].dataSource
			= XR_HAND_TRACKING_DATA_SOURCE_MAX_ENUM_EXT;
		return;
	}

	this->handPose.active = handActive;
	HOL::display::HandTransform[this->mSide].dataSource
		= XR_HAND_TRACKING_DATA_SOURCE_MAX_ENUM_EXT;
	if ((externalSample != nullptr || useHandTrackingDataSource)
		&& this->mDataSourceState.isActive)
	{
		HOL::display::HandTransform[this->mSide].dataSource = this->mDataSourceState.dataSource;
	}

	// Count tracked joints IMMEDIATELY after fetching, before any fallback/modification logic
	int trackedCount = 0;
	for (int i = 0; i < XR_HAND_JOINT_COUNT_EXT; i++)
	{
		if ((this->mJointLocations[i].locationFlags & XR_SPACE_LOCATION_POSITION_TRACKED_BIT)
			== XR_SPACE_LOCATION_POSITION_TRACKED_BIT)
		{
			trackedCount++;
		}
	}
	HOL::display::HandTransform[this->mSide].trackedJointCount = trackedCount;

	auto& palmLocation = this->mJointLocations[XrHandJointEXT::XR_HAND_JOINT_PALM_EXT];

	// Orientation is not going to be set without position for hand tracking.
	this->handPose.poseValid = (palmLocation.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)
							   == XR_SPACE_LOCATION_POSITION_VALID_BIT;

	// Use tracked joint count instead of just palm joint to determine tracking quality
	this->handPose.poseTracked = (trackedCount >= HOL::Config.general.minTrackedJointsForQuality);

	// If source is not unobstructed treat it as invalid.
	bool hasUnobstructedHandTrackingSource
		= !useHandTrackingDataSource
		  || (this->mDataSourceState.isActive
			  && this->mDataSourceState.dataSource
					 == XR_HAND_TRACKING_DATA_SOURCE_UNOBSTRUCTED_EXT);
	if (!hasUnobstructedHandTrackingSource)
	{
		this->handPose.active = false;
		this->handPose.poseValid = false;
		this->handPose.poseTracked = false;
	}

	// Oculus reports the thumb-joint basis rotated 90 degrees around the local Z axis.
	// Correct the raw joint orientations here so every downstream consumer sees the same fixed data.
	if (HOL::state::Runtime.isOVR)
	{
		applyOculusThumbOrientationFix(this->mJointLocations, mSide);
	}

	// Replace current data with past, but insert palm position from body tracker
	// Airlink provides incorrect values for anything but handtracking without controllers,
	// so using the body trackign values actually breaks things.
	bool alwaysUseUpperBodyTracking = false;	// Add to config later, probably does nothing though.
	bool usingBodyTrackingFallback = false;
	XrBodyJointLocationFB* bodyPalmJoint = nullptr;
	if (bodyTracker.active)
	{
		auto& candidate = bodyTracker.getLastJointLocations()[
			this->mSide == HandSide::LeftHand ? XR_BODY_JOINT_LEFT_HAND_PALM_FB
											 : XR_BODY_JOINT_RIGHT_HAND_PALM_FB];
		const bool positionValid
			= (candidate.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
		const bool orientationValid
			= (candidate.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
		if (positionValid && orientationValid)
		{
			bodyPalmJoint = &candidate;
		}
	}

	if (bodyPalmJoint != nullptr && !this->handPose.poseTracked)
	{
		// Copy prev to current to maintain finger pose
		std::copy(std::begin(mPrevJointLocations),
				  std::end(mPrevJointLocations),
				  std::begin(mJointLocations));

		palmLocation.pose.position = bodyPalmJoint->pose.position;
		palmLocation.pose.orientation = bodyPalmJoint->pose.orientation;

		this->handPose.active = true;
		this->handPose.poseValid = true;
		this->handPose.poseTracked = false; // otherwise estimated usign upper-body
		usingBodyTrackingFallback = true;
	}
	else if (bodyPalmJoint != nullptr && alwaysUseUpperBodyTracking)
	{
		// Just copy palm position from body.
		palmLocation.pose.position = bodyPalmJoint->pose.position;
		palmLocation.pose.orientation = bodyPalmJoint->pose.orientation;

		this->handPose.active = true;
		this->handPose.poseValid = true;
		this->handPose.poseTracked = this->handPose.poseTracked; // otherwise estimated usign upper-body
		usingBodyTrackingFallback = true;
	}
	


	if (HOL::Config.handPose.forceInactive)
	{
		this->handPose.poseValid = false;
		this->handPose.active = false;
		this->handPose.poseTracked = false;
	}

	// Hand tracking data starts off very noisy, and will often
	// flicker between tracking and not tracking. Body tracking
	// is a much more stable source of palm position data,
	// but is higher latency. Keep track of how long we have had
	// hand tracking data and blend betwene the two accordingly.
	if (!usingBodyTrackingFallback && this->handPose.poseTracked)
	{
		if (this->mDirectHandTrackingStartTime == 0)
		{
			this->mDirectHandTrackingStartTime = time;
		}
	}
	else
	{
		this->mDirectHandTrackingStartTime = 0;
	}

	bool poseStateChanged = this->handPose.active != prevActive
							|| this->handPose.poseValid != prevPoseValid
							|| this->handPose.poseTracked != prevPoseTracked;
	this->handPose.poseStale = false;

	if (this->handPose.poseValid)
	{
		Eigen::Vector3f newPalmPosition = toEigenVector(palmLocation.pose.position);
		Eigen::Quaternionf newPalmOrientation = toEigenQuaternion(palmLocation.pose.orientation);
		auto palmVelocity = this->mJointVelocities[XrHandJointEXT::XR_HAND_JOINT_PALM_EXT];
		float resumeBlendAlpha = 1.0f;

		if (!usingBodyTrackingFallback && bodyPalmJoint != nullptr
			&& this->mDirectHandTrackingStartTime > 0)
		{
			float blendDurationSeconds = HOL::Config.steamvr.handTrackingResumeBlendMS / 1000.0f;
			if (blendDurationSeconds > 0.0f)
			{
				float elapsedSeconds
					= (float)(time - this->mDirectHandTrackingStartTime) / 1000000000.0f;
				resumeBlendAlpha = std::clamp(elapsedSeconds / blendDurationSeconds, 0.0f, 1.0f);

				Eigen::Vector3f bodyPalmPosition = toEigenVector(bodyPalmJoint->pose.position);
				Eigen::Quaternionf bodyPalmOrientation
					= toEigenQuaternion(bodyPalmJoint->pose.orientation);

				newPalmPosition
					= bodyPalmPosition + resumeBlendAlpha * (newPalmPosition - bodyPalmPosition);
				newPalmOrientation
					= bodyPalmOrientation.slerp(resumeBlendAlpha, newPalmOrientation);
			}
		}

		//////////////
		// Staleness
		//////////////

		// Don't bother doing anything with stale data.
		// VDXR updates at intervals of 7-16ms, Airlink updates constantly.
		// Prediction also does nothing for VDXR.
		if (usingExternalSample && !usingBodyTrackingFallback)
		{
			this->handPose.poseStale = !poseStateChanged
				&& this->mPrevExternalUpdateGeneration
					== externalSample->updateGeneration;
			this->mPrevExternalUpdateGeneration = externalSample->updateGeneration;
		}
		else
		{
			this->handPose.poseStale = mHasPrevRawPose && !poseStateChanged
				&& this->mPrevRawPose.position.isApprox(newPalmPosition)
				&& this->mPrevRawPose.orientation.coeffs().isApprox(
					newPalmOrientation.coeffs());
			this->mPrevExternalUpdateGeneration = 0;
		}

		if (!this->handPose.poseStale)
		{
			HOL::display::updateTrackingRate(
				this->mLastPoseUpdateTime,
				HOL::display::HandTransform[this->mSide].updateRateMS);

			//////////////////////////////////
			// Update transform and velocity
			//////////////////////////////////
			HOL::PoseLocation rawPalmPose;
			rawPalmPose.position = newPalmPosition;
			rawPalmPose.orientation = newPalmOrientation;

			HOL::PoseVelocity rawPalmVelocity;
			rawPalmVelocity.linearVelocity = toEigenVector(palmVelocity.linearVelocity);
			rawPalmVelocity.angularVelocity = toEigenVector(palmVelocity.angularVelocity);

			if (usingBodyTrackingFallback)
			{
				// The fallback pose comes from body tracking, so the hand-joint velocity no longer
				// corresponds to the pose we are submitting.
				rawPalmVelocity.linearVelocity = Eigen::Vector3f::Zero();
				rawPalmVelocity.angularVelocity = Eigen::Vector3f::Zero();
			}
			else if (resumeBlendAlpha < 1.0f)
			{
				// Blend in hand velocity gradually when tracking resumes so SteamVR prediction
				// does not latch onto a bogus recovery sample.
				rawPalmVelocity.linearVelocity *= resumeBlendAlpha;
				rawPalmVelocity.angularVelocity *= resumeBlendAlpha;
			}

			rawPalmVelocity.linearVelocity
				*= HOL::Config.steamvr.poseSmoothing.linearVelocityMultiplier;
			rawPalmVelocity.angularVelocity
				*= HOL::Config.steamvr.poseSmoothing.angularVelocityMultiplier;

			const float positionSmoothingAlpha
				= getSmoothingAlpha(HOL::Config.steamvr.poseSmoothing.positionSmoothingMS
										+ triggerStabilizationSmoothingMS,
									this->mHasFilteredPalmPose,
									this->handPose.active != prevActive
										|| this->handPose.poseValid != prevPoseValid,
									this->mPrevFilteredSampleTime,
									time);
			const float rotationSmoothingAlpha
				= getSmoothingAlpha(HOL::Config.steamvr.poseSmoothing.rotationSmoothingMS
										+ triggerStabilizationSmoothingMS,
									this->mHasFilteredPalmPose,
									poseStateChanged,
									this->mPrevFilteredSampleTime,
									time);

			if (positionSmoothingAlpha >= 1.0f && rotationSmoothingAlpha >= 1.0f)
			{
				this->mFilteredPalmPose = rawPalmPose;
				this->mFilteredPalmVelocity = rawPalmVelocity;
			}
			else
			{
				this->mFilteredPalmPose.position
					= this->mFilteredPalmPose.position
					  + positionSmoothingAlpha
							* (rawPalmPose.position - this->mFilteredPalmPose.position);
				this->mFilteredPalmPose.orientation
					= this->mFilteredPalmPose.orientation.slerp(rotationSmoothingAlpha,
															   rawPalmPose.orientation);
				this->mFilteredPalmVelocity.linearVelocity
					= this->mFilteredPalmVelocity.linearVelocity
					  + positionSmoothingAlpha
							* (rawPalmVelocity.linearVelocity
							   - this->mFilteredPalmVelocity.linearVelocity);
				this->mFilteredPalmVelocity.angularVelocity
					= this->mFilteredPalmVelocity.angularVelocity
					  + rotationSmoothingAlpha
							* (rawPalmVelocity.angularVelocity
							   - this->mFilteredPalmVelocity.angularVelocity);
			}

			this->mHasFilteredPalmPose = true;
			this->mPrevFilteredSampleTime = time;

			this->handPose.palmLocation = this->mFilteredPalmPose;
			this->handPose.palmVelocity = this->mFilteredPalmVelocity;

			this->handPose.controllerLocation = this->handPose.palmLocation;

			// The final controller pose remains offset for local display and any logic that wants
			// the controller-aligned transform, but the raw palm values are kept separately so the
			// driver can construct DriverFromHead poses directly from the sensor origin.
			this->handPose.controllerVelocity = this->handPose.palmVelocity;

			/////////////
			// Offsets
			/////////////

			// We have a base offset configured to match what VDXR handtracking gives you.
			// The user-configurable offset is applied in addition to this.
			auto controllerOffset = Config.handPose.applyBaseOffset
				? HOL::getControllerBaseOffset()
				: HOL::PoseLocationEuler{Eigen::Vector3f(0, 0, 0), Eigen::Vector3f(0, 0, 0)};

			// Matches to controller position, matching what VD does
			Eigen::Vector3f controllerRotationOffset = controllerOffset.orientation;
			Eigen::Vector3f controllerTranslationOffset = controllerOffset.position;

			// Additional user-configurable offset, so it can be applied
			// either directly to the pose we submit, or used as an offset in the driver.
			Eigen::Vector3f userRotationOffset = Config.handPose.orientationOffset;
			Eigen::Vector3f userTranslationOffset = Config.handPose.positionOffset;

			if (this->mSide == HandSide::LeftHand)
			{
				// Base offsets are for the left hand
			}
			else
			{
				controllerRotationOffset = flipHandRotation(controllerRotationOffset);
				controllerTranslationOffset = flipHandTranslation(controllerTranslationOffset);

				userRotationOffset = flipHandRotation(userRotationOffset);
				userTranslationOffset = flipHandTranslation(userTranslationOffset);
			}

			//////////////////////
			// Controller offset
			//////////////////////

			// Remember to apply translation then rotation, always

			this->handPose.controllerLocation.position
				= HOL::translateLocal(this->handPose.controllerLocation.position,
									  this->handPose.controllerLocation.orientation,
									  controllerTranslationOffset);

			this->handPose.controllerLocation.orientation
				= HOL::rotateLocal(this->handPose.controllerLocation.orientation,
								   HOL::quaternionFromEulerAnglesDegrees(controllerRotationOffset));

			////////////////
			// User offset
			///////////////

			this->handPose.controllerLocation.position
				= HOL::translateLocal(this->handPose.controllerLocation.position,
									  this->handPose.controllerLocation.orientation,
									  userTranslationOffset);

			// We need to perform this completely separate for the offset to match when
			// it is later applied to the native controller pose in fallback-only hooked mode.
			this->handPose.controllerLocation.orientation
				= HOL::rotateLocal(this->handPose.controllerLocation.orientation,
								   HOL::quaternionFromEulerAnglesDegrees(userRotationOffset));

			//////////////////////
			// Finger movement
			//////////////////////

			this->calculateCurlSplay();

			/////////////
			// Prev
			/////////////

			// Set prev
			this->mPrevRawPose.position = newPalmPosition;
			this->mPrevRawPose.orientation = newPalmOrientation;
			this->mHasPrevRawPose = true;

			///////////////////////////
			// Update display values
			///////////////////////////
			{
				// Hand orientation

				HOL::display::HandTransform[this->mSide].rawPose.position = newPalmPosition;
				HOL::display::HandTransform[this->mSide].rawPose.orientation = newPalmOrientation;

				HOL::display::HandTransform[this->mSide].finalPose.position
					= this->handPose.controllerLocation.position;
				HOL::display::HandTransform[this->mSide].finalPose.orientation
					= this->handPose.controllerLocation.orientation;

				HOL::display::HandTransform[this->mSide].finalTranslationOffset
					= controllerTranslationOffset;
				HOL::display::HandTransform[this->mSide].finalOrientationOffset
					= controllerRotationOffset;

				// Finger curl
				for (int i = 0; i < FingerType_MAX; i++)
				{
					// Turns out you cannot assign an array to an array
					HOL::display::FingerTracking[this->mSide].rawBend[i]
						= this->handPose.fingers[i];
				}
			}
		}
	}
	else
	{
		this->handPose.poseStale = !poseStateChanged;
		this->mHasPrevRawPose = false;
		this->mHasFilteredPalmPose = false;
		this->mPrevExternalUpdateGeneration = 0;
		this->mPrevFilteredSampleTime = 0;
		this->mLastPoseUpdateTime = {};
		HOL::display::HandTransform[this->mSide].updateRateMS.store(0.0f);
	}

	mPrevActive = this->handPose.active;
	mPrevPoseValid = this->handPose.poseValid;
	mPrevPoseTracked = this->handPose.poseTracked;

	///////////////////////////
	// Update display values
	///////////////////////////

	{
		HOL::display::HandTransform[this->mSide].active = this->handPose.active;
		HOL::display::HandTransform[this->mSide].positionValid = this->handPose.poseValid;
		HOL::display::HandTransform[this->mSide].positionTracked = this->handPose.poseTracked;
	}
}
