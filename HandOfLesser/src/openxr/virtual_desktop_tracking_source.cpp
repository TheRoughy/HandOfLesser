#include "virtual_desktop_tracking_source.h"

#include <Eigen/Geometry>

#include <cmath>
#include <cstring>
#include <iostream>

namespace HOL::VirtualDesktop
{
	namespace
	{
		bool hasValidPosition(uint64_t flags)
		{
			return (flags & XR_SPACE_LOCATION_POSITION_VALID_BIT) != 0;
		}

		bool hasValidPose(uint64_t flags)
		{
			return hasValidPosition(flags)
				   && (flags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) != 0;
		}

		XrPosef toXrPose(const Pose& pose, float floorOffset)
		{
			XrPosef result{};
			result.orientation
				= {pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w};
			result.position = {pose.position.x, pose.position.y + floorOffset, pose.position.z};
			return result;
		}

		bool
		handDataChanged(HOL::HandSide side, const BodyStateV2& current, const BodyStateV2& previous)
		{
			const bool currentActive
				= side == HOL::LeftHand ? current.leftHandActive : current.rightHandActive;
			const bool previousActive
				= side == HOL::LeftHand ? previous.leftHandActive : previous.rightHandActive;
			if (currentActive != previousActive)
			{
				return true;
			}

			const auto& currentJoints = side == HOL::LeftHand ? current.leftHandJointStates
															  : current.rightHandJointStates;
			const auto& previousJoints = side == HOL::LeftHand ? previous.leftHandJointStates
															   : previous.rightHandJointStates;
			if (std::memcmp(currentJoints, previousJoints, sizeof(currentJoints)) != 0)
			{
				return true;
			}

			const auto& currentAim
				= side == HOL::LeftHand ? current.leftAimState : current.rightAimState;
			const auto& previousAim
				= side == HOL::LeftHand ? previous.leftAimState : previous.rightAimState;
			return std::memcmp(&currentAim, &previousAim, sizeof(currentAim)) != 0;
		}

		bool bodyDataChanged(const BodyStateV2& current, const BodyStateV2& previous)
		{
			return current.bodyTrackingConfidence != previous.bodyTrackingConfidence
				   || std::memcmp(
						  current.bodyJoints, previous.bodyJoints, sizeof(current.bodyJoints))
						  != 0;
		}

		bool trackingDataChanged(const BodyStateV2& current, const BodyStateV2& previous)
		{
			// Face and eye data share this mapping but must not keep hand/body tracking fresh.
			return handDataChanged(HOL::LeftHand, current, previous)
				   || handDataChanged(HOL::RightHand, current, previous)
				   || bodyDataChanged(current, previous);
		}
	} // namespace

	VirtualDesktopTrackingSource::VirtualDesktopTrackingSource()
	{
		for (auto& sample : mHandSamples)
		{
			sample.hasUpdateGeneration = true;
			sample.dataSourceState = {XR_TYPE_HAND_TRACKING_DATA_SOURCE_STATE_EXT};
			sample.aimState = {XR_TYPE_HAND_TRACKING_AIM_STATE_FB};
		}
		mBodySample.hasUpdateGeneration = true;
	}

	VirtualDesktopTrackingSource::~VirtualDesktopTrackingSource()
	{
		disconnect();
	}

	bool VirtualDesktopTrackingSource::connect()
	{
		mMapping = OpenFileMappingW(FILE_MAP_READ, FALSE, L"VirtualDesktop.BodyState");
		if (mMapping == nullptr)
		{
			if (!mReportedWaiting)
			{
				std::cout << "Waiting for Virtual Desktop shared tracking data." << std::endl;
				mReportedWaiting = true;
			}
			return false;
		}

		mMappedState = static_cast<const BodyStateV2*>(
			MapViewOfFile(mMapping, FILE_MAP_READ, 0, 0, sizeof(BodyStateV2)));
		if (mMappedState == nullptr)
		{
			CloseHandle(mMapping);
			mMapping = nullptr;
			if (!mReportedWaiting)
			{
				std::cout << "Virtual Desktop shared tracking data has an incompatible layout."
						  << std::endl;
				mReportedWaiting = true;
			}
			return false;
		}

		std::cout << "Connected to Virtual Desktop shared tracking data." << std::endl;
		mReportedWaiting = false;
		mSourceFresh = false;
		mFloorOffsetInitialized = false;
		mFloorOffset = 0.0f;
		return true;
	}

	void VirtualDesktopTrackingSource::disconnect()
	{
		if (mMappedState != nullptr)
		{
			UnmapViewOfFile(mMappedState);
			mMappedState = nullptr;
		}
		if (mMapping != nullptr)
		{
			CloseHandle(mMapping);
			mMapping = nullptr;
		}
	}

	bool VirtualDesktopTrackingSource::readStableSnapshot(BodyStateV2& snapshot) const
	{
		if (mMappedState == nullptr)
		{
			return false;
		}

		BodyStateV2 first{};
		BodyStateV2 second{};
		std::memcpy(&first, mMappedState, sizeof(first));
		MemoryBarrier();

		// The mapping has no sequence counter or reader lock. Accept only two matching copies to
		// avoid consuming a snapshot while Virtual Desktop is partway through updating it.
		for (int attempt = 0; attempt < 3; attempt++)
		{
			std::memcpy(&second, mMappedState, sizeof(second));
			MemoryBarrier();
			if (std::memcmp(&first, &second, sizeof(first)) == 0)
			{
				snapshot = second;
				return true;
			}
			first = second;
		}

		return false;
	}

	void VirtualDesktopTrackingSource::initializeFloorOffset(const BodyStateV2& snapshot)
	{
		if (mFloorOffsetInitialized)
		{
			return;
		}

		// VDXR adds the Oculus-configured eye height before exposing this data through OpenXR.
		// Direct shared-memory access has no eye-height field, so infer the equivalent translation
		// from the feet when full-body data is available.
		float footHeight = 0.0f;
		int validFeet = 0;
		for (const XrFullBodyJointMETA foot :
			 {XR_FULL_BODY_JOINT_LEFT_FOOT_BALL_META, XR_FULL_BODY_JOINT_RIGHT_FOOT_BALL_META})
		{
			const auto& joint = snapshot.bodyJoints[foot];
			if (hasValidPosition(joint.locationFlags))
			{
				footHeight += joint.pose.position.y;
				validFeet++;
			}
		}

		if (validFeet > 0)
		{
			// The full-body feet reveal whether VD supplied floor-relative or eye-relative poses.
			mFloorOffset = -(footHeight / static_cast<float>(validFeet));
			if (mFloorOffset < -0.25f || mFloorOffset > 2.5f)
			{
				mFloorOffset = 0.0f;
			}
		}
		else
		{
			// A head near Y=0 confirms eye-relative data when feet are unavailable.
			const auto& head = snapshot.bodyJoints[XR_FULL_BODY_JOINT_HEAD_META];
			if (!hasValidPosition(head.locationFlags))
			{
				return;
			}
			mFloorOffset = std::abs(head.pose.position.y) < 0.5f ? DefaultEyeHeight : 0.0f;
		}

		mFloorOffsetInitialized = true;
	}

	void VirtualDesktopTrackingSource::updateHandSample(HOL::HandSide side,
														const BodyStateV2& snapshot,
														const BodyStateV2* previous,
														bool forceUpdate)
	{
		if (!forceUpdate && previous != nullptr && !handDataChanged(side, snapshot, *previous))
		{
			return;
		}

		const bool active
			= side == HOL::LeftHand ? snapshot.leftHandActive : snapshot.rightHandActive;
		const auto& sourceJoints
			= side == HOL::LeftHand ? snapshot.leftHandJointStates : snapshot.rightHandJointStates;
		const auto& sourceAim
			= side == HOL::LeftHand ? snapshot.leftAimState : snapshot.rightAimState;

		auto& sample = mHandSamples[side];
		sample = {};
		sample.active = active;
		sample.hasUpdateGeneration = true;
		sample.updateGeneration = ++mHandGenerations[side];
		sample.aimState = {XR_TYPE_HAND_TRACKING_AIM_STATE_FB};
		sample.dataSourceState = {XR_TYPE_HAND_TRACKING_DATA_SOURCE_STATE_EXT};
		if (!active)
		{
			return;
		}

		// Unlike the body joints, VD's hand-joint records have no per-joint flags. This matches
		// VDXR's behavior of treating every joint as valid and tracked while the hand is active.
		constexpr XrSpaceLocationFlags locationFlags
			= XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT
			  | XR_SPACE_LOCATION_POSITION_TRACKED_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
		constexpr XrSpaceVelocityFlags velocityFlags
			= XR_SPACE_VELOCITY_LINEAR_VALID_BIT | XR_SPACE_VELOCITY_ANGULAR_VALID_BIT;
		for (size_t jointIndex = 0; jointIndex < HandJointCount; jointIndex++)
		{
			const auto& source = sourceJoints[jointIndex];
			auto& location = sample.joints[jointIndex];
			location.locationFlags = locationFlags;
			location.pose = toXrPose(source.pose, mFloorOffset);
			location.radius = source.radius;

			auto& velocity = sample.velocities[jointIndex];
			velocity.velocityFlags = velocityFlags;
			velocity.linearVelocity
				= {source.linearVelocity.x, source.linearVelocity.y, source.linearVelocity.z};
			velocity.angularVelocity
				= {source.angularVelocity.x, source.angularVelocity.y, source.angularVelocity.z};
		}

		sample.aimState.status = sourceAim.aimStatus;
		sample.aimState.aimPose = toXrPose(sourceAim.aimPose, mFloorOffset);
		sample.aimState.pinchStrengthIndex = sourceAim.pinchStrengthIndex;
		sample.aimState.pinchStrengthMiddle = sourceAim.pinchStrengthMiddle;
		sample.aimState.pinchStrengthRing = sourceAim.pinchStrengthRing;
		sample.aimState.pinchStrengthLittle = sourceAim.pinchStrengthLittle;
		sample.dataSourceState.isActive = XR_TRUE;
		sample.dataSourceState.dataSource = XR_HAND_TRACKING_DATA_SOURCE_UNOBSTRUCTED_EXT;
	}

	void VirtualDesktopTrackingSource::updateBodySample(const BodyStateV2& snapshot,
														const BodyStateV2* previous,
														bool forceUpdate)
	{
		if (!forceUpdate && previous != nullptr && !bodyDataChanged(snapshot, *previous))
		{
			return;
		}

		mBodySample = {};
		mBodySample.active = snapshot.bodyTrackingConfidence > 0.0f;
		mBodySample.confidence = snapshot.bodyTrackingConfidence;
		mBodySample.hasUpdateGeneration = true;
		mBodySample.updateGeneration = ++mBodyGeneration;
		if (!mBodySample.active)
		{
			return;
		}

		// The shared ABI contains META's 84-joint full-body set, whose first 70 entries are the
		// FB body-joint layout consumed by the existing HandOfLesser body pipeline.
		for (size_t jointIndex = 0; jointIndex < XR_BODY_JOINT_COUNT_FB; jointIndex++)
		{
			const auto& source = snapshot.bodyJoints[jointIndex];
			auto& location = mBodySample.joints[jointIndex];
			location.locationFlags = source.locationFlags;
			if (hasValidPose(source.locationFlags))
			{
				location.pose = toXrPose(source.pose, mFloorOffset);
			}
		}
	}

	void VirtualDesktopTrackingSource::updateTrackingReference(const BodyStateV2& snapshot)
	{
		const auto& head = snapshot.bodyJoints[XR_FULL_BODY_JOINT_HEAD_META];
		if (!hasValidPose(head.locationFlags))
		{
			mHmdPose.reset();
			return;
		}

		HOL::PoseLocation pose;
		pose.position = Eigen::Vector3f(
			head.pose.position.x, head.pose.position.y + mFloorOffset, head.pose.position.z);
		const Eigen::Quaternionf bodyOrientation(head.pose.orientation.w,
												 head.pose.orientation.x,
												 head.pose.orientation.y,
												 head.pose.orientation.z);
		// Body head joints and HMD view poses use different local forward axes. Undo the body-joint
		// basis adjustment so spatial gestures receive the same orientation an HMD pose would use.
		const Eigen::Quaternionf viewToBody = Eigen::Quaternionf::FromTwoVectors(
			Eigen::Vector3f::UnitY(), Eigen::Vector3f(0.0f, 0.0f, -1.0f));
		pose.orientation = (bodyOrientation * viewToBody.inverse()).normalized();
		mHmdPose = pose;
	}

	void VirtualDesktopTrackingSource::updateSamples(const BodyStateV2& snapshot, bool forceUpdate)
	{
		const BodyStateV2* previous = mHasState ? &mLastState : nullptr;
		const bool hadFloorOffset = mFloorOffsetInitialized;
		initializeFloorOffset(snapshot);
		// Rebuild every cached pose when the floor translation first becomes available, even if
		// that hand or body region did not change in the shared snapshot.
		forceUpdate = forceUpdate || (!hadFloorOffset && mFloorOffsetInitialized);
		updateHandSample(HOL::LeftHand, snapshot, previous, forceUpdate);
		updateHandSample(HOL::RightHand, snapshot, previous, forceUpdate);
		updateBodySample(snapshot, previous, forceUpdate);
		updateTrackingReference(snapshot);
	}

	void VirtualDesktopTrackingSource::setSamplesInactive()
	{
		for (int side = 0; side < HOL::HandSide_MAX; side++)
		{
			auto& sample = mHandSamples[side];
			if (sample.active)
			{
				sample = {};
				sample.hasUpdateGeneration = true;
				sample.updateGeneration = ++mHandGenerations[side];
				sample.aimState = {XR_TYPE_HAND_TRACKING_AIM_STATE_FB};
				sample.dataSourceState = {XR_TYPE_HAND_TRACKING_DATA_SOURCE_STATE_EXT};
			}
		}

		if (mBodySample.active)
		{
			mBodySample = {};
			mBodySample.hasUpdateGeneration = true;
			mBodySample.updateGeneration = ++mBodyGeneration;
		}
		mHmdPose.reset();
		mSourceFresh = false;
	}

	HOL::TrackingSourceFrame VirtualDesktopTrackingSource::update(bool, bool)
	{
		const auto now = std::chrono::steady_clock::now();
		if (mMappedState == nullptr && now >= mNextConnectAttempt)
		{
			if (!connect())
			{
				mNextConnectAttempt = now + ReconnectInterval;
			}
		}

		if (mMappedState != nullptr)
		{
			BodyStateV2 snapshot{};
			if (readStableSnapshot(snapshot))
			{
				const bool stateChanged = !mHasState || trackingDataChanged(snapshot, mLastState);
				if (stateChanged)
				{
					const bool forceUpdate = !mHasState || !mSourceFresh;
					updateSamples(snapshot, forceUpdate);
					mLastState = snapshot;
					mHasState = true;
					mSourceFresh = true;
					mLastStateChange = now;
				}
				else if (now - mLastStateChange >= StaleTimeout)
				{
					if (mSourceFresh)
					{
						setSamplesInactive();
					}
					// Closing our handle lets a restarted VD process replace the named mapping.
					disconnect();
					mNextConnectAttempt = now + ReconnectInterval;
				}
			}
		}

		HOL::TrackingSourceFrame frame;
		// The shared block has no timestamp. A monotonic local timestamp keeps filtering and body
		// fallback timing in the same nanosecond units used by OpenXR.
		frame.time
			= std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
		frame.hmdPose = mHmdPose;
		for (int side = 0; side < HOL::HandSide_MAX; side++)
		{
			frame.hands[side] = &mHandSamples[side];
		}
		// A missing VD body sample should use the normal HMD-based synthetic body fallback.
		frame.body = mBodySample.active ? &mBodySample : nullptr;
		return frame;
	}

	bool VirtualDesktopTrackingSource::hasTrackingReference() const
	{
		return mSourceFresh && mHmdPose.has_value();
	}

	void VirtualDesktopTrackingSource::reset()
	{
		disconnect();
		mHasState = false;
		mSourceFresh = false;
		mFloorOffsetInitialized = false;
		mFloorOffset = 0.0f;
		mNextConnectAttempt = {};
		mReportedWaiting = false;
		setSamplesInactive();
	}
} // namespace HOL::VirtualDesktop
