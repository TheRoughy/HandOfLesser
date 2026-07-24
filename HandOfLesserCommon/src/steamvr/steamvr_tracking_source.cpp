#include "steamvr_tracking_source.h"

#include "src/controller/controller.h"
#include "src/steamvr/skeletal_pose_utils.h"

#include <chrono>

namespace HOL::SteamVR
{
	void SteamVRTrackingSource::updateBaseline(const HOL::SteamVRHandBaselinePayload& payload)
	{
		if (payload.side < HOL::LeftHand || payload.side >= HOL::HandSide_MAX)
		{
			return;
		}

		// The pipe thread publishes immutable snapshots; the tracking thread consumes them later.
		mBaselines[payload.side].store(
			std::make_shared<const HOL::SteamVRHandBaselinePayload>(payload));
	}

	void SteamVRTrackingSource::updatePose(const HOL::SteamVRHandPosePayload& payload)
	{
		if (payload.side < HOL::LeftHand || payload.side >= HOL::HandSide_MAX)
		{
			return;
		}

		mPoses[payload.side].store(std::make_shared<const HOL::SteamVRHandPosePayload>(payload));
	}

	void SteamVRTrackingSource::updateHmdPose(const HOL::SteamVRHmdPosePayload& payload)
	{
		mHmdPose.store(std::make_shared<const HOL::SteamVRHmdPosePayload>(payload));
	}

	void SteamVRTrackingSource::setInactive(SideState& state)
	{
		// Publish the active-to-inactive transition once without manufacturing updates every frame.
		const bool changed = state.sample.active || state.hasSourcePose;
		state.sample = {};
		state.hasSourcePose = false;
		if (changed)
		{
			state.sampleGeneration++;
		}
		state.sample.hasUpdateGeneration = true;
		state.sample.updateGeneration = state.sampleGeneration;
	}

	const HOL::HandTrackingSample*
	SteamVRTrackingSource::getSample(HOL::HandSide side, bool applyBaseOffset, bool skeletalUpdate)
	{
		if (side < HOL::LeftHand || side >= HOL::HandSide_MAX)
		{
			return nullptr;
		}

		const auto baseline = mBaselines[side].load();
		if (!baseline)
		{
			auto& state = mStates[side];
			setInactive(state);
			return &state.sample;
		}

		auto& state = mStates[side];
		const bool sourceChanged
			= state.hasBaseline && state.sourceDeviceId != baseline->sourceDeviceId;
		const bool activeChanged = !state.hasBaseline || state.sample.active != baseline->active;
		const bool skeletonChanged
			= !state.hasRelativeJoints || state.skeletonGeneration != baseline->skeletonGeneration;
		const bool offsetChanged = state.hasBaseline && state.applyBaseOffset != applyBaseOffset;

		state.hasBaseline = true;
		state.sourceDeviceId = baseline->sourceDeviceId;
		state.applyBaseOffset = applyBaseOffset;
		if (sourceChanged)
		{
			state.hasRelativeJoints = false;
		}

		if (!baseline->active)
		{
			state.hasRelativeJoints = false;
			setInactive(state);
			return &state.sample;
		}

		if (skeletalUpdate && (skeletonChanged || sourceChanged))
		{
			buildOpenXRPalmRelativeJointPoseFromSkeletalPose(
				side, baseline->transforms, state.relativeJoints);
			state.skeletonGeneration = baseline->skeletonGeneration;
			state.hasRelativeJoints = true;
		}

		// The baseline carries a usable pose, but prefer a newer delta from the same source.
		const vr::DriverPose_t* controllerPose = &baseline->pose;
		uint64_t poseGeneration = baseline->poseGeneration;
		const auto pose = mPoses[side].load();
		if (pose && pose->active && pose->sourceDeviceId == baseline->sourceDeviceId
			&& pose->poseGeneration > poseGeneration)
		{
			controllerPose = &pose->pose;
			poseGeneration = pose->poseGeneration;
		}

		const bool poseChanged = !state.hasSourcePose || state.poseGeneration != poseGeneration;
		const bool sampleChanged = sourceChanged || activeChanged || poseChanged || offsetChanged
								   || (skeletalUpdate && skeletonChanged);
		if (!sampleChanged && !skeletalUpdate)
		{
			return &state.sample;
		}

		const HOL::PoseLocation steamVRControllerPose = getSteamVRDevicePose(*controllerPose);
		// Native hand controllers already include our base palm-to-controller alignment. Undo only
		// that base offset to recover the palm; user offsets are applied to our output later.
		const HOL::PoseLocation controllerOffset = HOL::getControllerPoseOffset(
			side, applyBaseOffset, Eigen::Vector3f::Zero(), Eigen::Vector3f::Zero());
		HOL::PoseLocation steamVRPalmPose;
		steamVRPalmPose.orientation
			= steamVRControllerPose.orientation * controllerOffset.orientation.inverse();
		steamVRPalmPose.position = steamVRControllerPose.position
								   - steamVRPalmPose.orientation * controllerOffset.position;

		const HOL::PoseLocation palmPose = steamVRPalmPose;

		if (sourceChanged || activeChanged)
		{
			state.sample = {};
		}
		state.sample.active = true;
		state.sample.hasUpdateGeneration = true;
		state.sample.dataSourceState.isActive = true;
		state.sample.dataSourceState.dataSource = XR_HAND_TRACKING_DATA_SOURCE_UNOBSTRUCTED_EXT;

		if (skeletalUpdate && state.hasRelativeJoints)
		{
			HOL::PoseLocation jointPoses[XR_HAND_JOINT_COUNT_EXT]{};
			applySteamVRPoseToOpenXRJointPose(palmPose, state.relativeJoints, jointPoses);
			for (int i = 0; i < XR_HAND_JOINT_COUNT_EXT; i++)
			{
				auto& location = state.sample.joints[i];
				location.locationFlags = XR_SPACE_LOCATION_POSITION_VALID_BIT
										 | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT
										 | XR_SPACE_LOCATION_POSITION_TRACKED_BIT
										 | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
				location.pose.position = {jointPoses[i].position.x(),
										  jointPoses[i].position.y(),
										  jointPoses[i].position.z()};
				location.pose.orientation = {jointPoses[i].orientation.x(),
											 jointPoses[i].orientation.y(),
											 jointPoses[i].orientation.z(),
											 jointPoses[i].orientation.w()};
			}
		}

		// Pose-only updates keep the cached finger joints but move the palm immediately.
		auto& palmLocation = state.sample.joints[XR_HAND_JOINT_PALM_EXT];
		palmLocation.locationFlags
			= XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT
			  | XR_SPACE_LOCATION_POSITION_TRACKED_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
		palmLocation.pose.position
			= {palmPose.position.x(), palmPose.position.y(), palmPose.position.z()};
		palmLocation.pose.orientation = {palmPose.orientation.x(),
										 palmPose.orientation.y(),
										 palmPose.orientation.z(),
										 palmPose.orientation.w()};

		// Only palm velocity is consumed downstream; finger motion remains relative to the palm.
		HOL::PoseVelocity palmVelocity
			= getSteamVRVelocityAtPosition(*controllerPose, steamVRPalmPose.position);
		auto& velocity = state.sample.velocities[XR_HAND_JOINT_PALM_EXT];
		velocity.velocityFlags
			= XR_SPACE_VELOCITY_LINEAR_VALID_BIT | XR_SPACE_VELOCITY_ANGULAR_VALID_BIT;
		velocity.linearVelocity = {palmVelocity.linearVelocity.x(),
								   palmVelocity.linearVelocity.y(),
								   palmVelocity.linearVelocity.z()};
		velocity.angularVelocity = {palmVelocity.angularVelocity.x(),
									palmVelocity.angularVelocity.y(),
									palmVelocity.angularVelocity.z()};

		if (sampleChanged)
		{
			state.sourcePose = *controllerPose;
			state.hasSourcePose = true;
			state.poseGeneration = poseGeneration;
			state.sampleGeneration++;
		}
		state.sample.updateGeneration = state.sampleGeneration;
		return &state.sample;
	}

	HOL::TrackingSourceFrame SteamVRTrackingSource::update(bool skeletalUpdate,
														   bool applyBaseOffset)
	{
		if (mResetRequested.exchange(false))
		{
			mStates = {};
		}

		HOL::TrackingSourceFrame frame;
		// Only elapsed time is consumed downstream, so this does not need the OpenXR runtime's epoch.
		frame.time = std::chrono::duration_cast<std::chrono::nanoseconds>(
						 std::chrono::steady_clock::now().time_since_epoch())
						 .count();

		const auto hmdPose = mHmdPose.load();
		if (hmdPose && hmdPose->active)
		{
			frame.hmdPose = getSteamVRDevicePose(hmdPose->pose);
		}

		for (int side = 0; side < HOL::HandSide_MAX; side++)
		{
			frame.hands[side]
				= getSample(static_cast<HOL::HandSide>(side), applyBaseOffset, skeletalUpdate);
		}
		return frame;
	}

	void SteamVRTrackingSource::applySourcePose(HOL::HandSide side,
												HOL::HandTransformPayload& payload) const
	{
		if (side < HOL::LeftHand || side >= HOL::HandSide_MAX)
		{
			return;
		}

		const auto& state = mStates[side];
		if (!state.hasSourcePose)
		{
			return;
		}

		// Keep the native pose as an output template so the driver retains its prediction and
		// coordinate metadata. Processed poses already use the same SteamVR world space.
		payload.hasSteamVRSourcePose = true;
		payload.steamVRSourcePose = state.sourcePose;
	}

	bool SteamVRTrackingSource::hasTrackingReference() const
	{
		const auto hmdPose = mHmdPose.load();
		return hmdPose && hmdPose->active;
	}

	void SteamVRTrackingSource::reset()
	{
		for (auto& baseline : mBaselines)
		{
			baseline.store(nullptr);
		}
		for (auto& pose : mPoses)
		{
			pose.store(nullptr);
		}
		mHmdPose.store(nullptr);
		mResetRequested.store(true);
	}
} // namespace HOL::SteamVR
