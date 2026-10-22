#include "fallback_tracker_source.h"

#include <openvr.h>
#include <iostream>

namespace HOL::SteamVR
{
	FallbackTrackerSource::~FallbackTrackerSource()
	{
		reset();
	}

	bool FallbackTrackerSource::connect()
	{
		const auto now = std::chrono::steady_clock::now();
		if (now < mNextConnectAttempt)
		{
			return false;
		}
		mNextConnectAttempt = now + std::chrono::seconds(1);

		// Background clients neither launch SteamVR nor occupy its scene-application slot.
		vr::EVRInitError error = vr::VRInitError_None;
		mSystem = vr::VR_Init(&error, vr::VRApplication_Background);
		if (error != vr::VRInitError_None || !mSystem)
		{
			mSystem = nullptr;
			if (!mReportedWaiting)
			{
				std::cout << "Waiting for SteamVR fallback tracker poses: "
						  << vr::VR_GetVRInitErrorAsEnglishDescription(error) << std::endl;
				mReportedWaiting = true;
			}
			return false;
		}
		mDeviceIds.fill(vr::k_unTrackedDeviceIndexInvalid);
		mNextDeviceRefresh = {};
		mReportedWaiting = false;
		std::cout << "Connected to calibrated SteamVR fallback tracker poses." << std::endl;
		return true;
	}

	void FallbackTrackerSource::resetAlignment()
	{
		mTrackingFromStanding.reset();
	}

	void FallbackTrackerSource::reset()
	{
		if (mSystem)
		{
			vr::VR_Shutdown();
			mSystem = nullptr;
		}
		resetAlignment();
		mDeviceIds.fill(vr::k_unTrackedDeviceIndexInvalid);
		mSerials = {};
		mNextConnectAttempt = {};
		mNextDeviceRefresh = {};
		mReportedWaiting = false;
	}

	void
	FallbackTrackerSource::findTrackers(const std::array<std::string, HOL::HandSide_MAX>& serials)
	{
		mSerials = serials;
		mDeviceIds.fill(vr::k_unTrackedDeviceIndexInvalid);
		for (uint32_t id = 0; id < vr::k_unMaxTrackedDeviceCount; id++)
		{
			if (mSystem->GetTrackedDeviceClass(id) != vr::TrackedDeviceClass_GenericTracker)
			{
				continue;
			}
			char serial[vr::k_unMaxPropertyStringSize]{};
			vr::ETrackedPropertyError error = vr::TrackedProp_Success;
			mSystem->GetStringTrackedDeviceProperty(
				id, vr::Prop_SerialNumber_String, serial, sizeof(serial), &error);
			if (error != vr::TrackedProp_Success)
			{
				continue;
			}
			for (int side = 0; side < HOL::HandSide_MAX; side++)
			{
				if (!serials[side].empty() && serials[side] == serial)
				{
					mDeviceIds[side] = id;
				}
			}
		}
		mNextDeviceRefresh = std::chrono::steady_clock::now() + std::chrono::seconds(1);
	}

	std::optional<HOL::PoseLocation>
	FallbackTrackerSource::toPoseLocation(const vr::TrackedDevicePose_t& pose)
	{
		if (!pose.bDeviceIsConnected || !pose.bPoseIsValid
			|| pose.eTrackingResult != vr::TrackingResult_Running_OK)
		{
			return std::nullopt;
		}
		const auto& matrix = pose.mDeviceToAbsoluteTracking.m;
		Eigen::Matrix3f rotation;
		for (int row = 0; row < 3; row++)
		{
			for (int column = 0; column < 3; column++)
			{
				rotation(row, column) = matrix[row][column];
			}
		}
		HOL::PoseLocation location{Eigen::Vector3f(matrix[0][3], matrix[1][3], matrix[2][3]),
								   Eigen::Quaternionf(rotation)};
		if (!location.position.allFinite() || !location.orientation.coeffs().allFinite()
			|| location.orientation.squaredNorm() < 0.000001f)
		{
			return std::nullopt;
		}
		location.orientation.normalize();
		return location;
	}

	FallbackTrackerSource::TrackerPoses
	FallbackTrackerSource::update(const std::array<std::string, HOL::HandSide_MAX>& serials,
								  const HOL::PoseLocation* sourceHmdPose)
	{
		TrackerPoses result{};
		if (serials[HOL::LeftHand].empty() && serials[HOL::RightHand].empty())
		{
			reset();
			return result;
		}
		if (!mSystem && !connect())
		{
			return result;
		}

		vr::VREvent_t event{};
		while (mSystem->PollNextEvent(&event, sizeof(event)))
		{
			switch (event.eventType)
			{
				case vr::VREvent_Quit:
					reset();
					mNextConnectAttempt
						= std::chrono::steady_clock::now() + std::chrono::seconds(1);
					return result;
				case vr::VREvent_ChaperoneUniverseHasChanged:
				case vr::VREvent_SeatedZeroPoseReset:
				case vr::VREvent_StandingZeroPoseReset:
					resetAlignment();
					break;
				case vr::VREvent_TrackedDeviceActivated:
				case vr::VREvent_TrackedDeviceDeactivated:
				case vr::VREvent_TrackedDeviceUpdated:
					mNextDeviceRefresh = {};
					break;
			}
		}
		if (mSerials != serials || std::chrono::steady_clock::now() >= mNextDeviceRefresh)
		{
			findTrackers(serials);
		}

		// Standing poses include calibration add-ons regardless of the order their hooks ran.
		// GetRawTrackedDevicePoses would leave us with driver-specific reference spaces instead.
		// Tracker recalibration is reflected here immediately; only the origin mapping is cached.
		vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
		mSystem->GetDeviceToAbsoluteTrackingPose(
			vr::TrackingUniverseStanding, 0.0f, poses, vr::k_unMaxTrackedDeviceCount);
		if (!mTrackingFromStanding)
		{
			const auto standingHmd = toPoseLocation(poses[vr::k_unTrackedDeviceIndex_Hmd]);
			if (!standingHmd || !sourceHmdPose || !sourceHmdPose->position.allFinite()
				|| !sourceHmdPose->orientation.coeffs().allFinite()
				|| sourceHmdPose->orientation.squaredNorm() < 0.000001f)
			{
				return result;
			}
			// The same HMD in both spaces gives us the rotation and translation between origins.
			// Calibration add-ons are assumed to move other devices into the HMD's tracking space,
			// leaving the HMD itself unchanged. That space is shared with the hand providers.
			HOL::PoseLocation alignment;
			alignment.orientation
				= (sourceHmdPose->orientation.normalized() * standingHmd->orientation.inverse())
					  .normalized();
			alignment.position
				= sourceHmdPose->position - alignment.orientation * standingHmd->position;
			mTrackingFromStanding = alignment;
		}

		for (int side = 0; side < HOL::HandSide_MAX; side++)
		{
			const auto id = mDeviceIds[side];
			if (id == vr::k_unTrackedDeviceIndexInvalid)
			{
				continue;
			}
			auto tracker = toPoseLocation(poses[id]);
			if (tracker)
			{
				tracker->position = mTrackingFromStanding->position
									+ mTrackingFromStanding->orientation * tracker->position;
				tracker->orientation
					= (mTrackingFromStanding->orientation * tracker->orientation).normalized();
				TrackerPose sample;
				sample.location = *tracker;
				const auto& pose = poses[id];
				const Eigen::Vector3f linearVelocity(
					pose.vVelocity.v[0], pose.vVelocity.v[1], pose.vVelocity.v[2]);
				const Eigen::Vector3f angularVelocity(
					pose.vAngularVelocity.v[0], pose.vAngularVelocity.v[1], pose.vAngularVelocity.v[2]);
				// Velocities use the same reference-space rotation as positions, but no translation.
				// A bad velocity must not invalidate an otherwise usable tracker pose.
				if (linearVelocity.allFinite())
				{
					sample.velocity.linearVelocity = mTrackingFromStanding->orientation * linearVelocity;
				}
				if (angularVelocity.allFinite())
				{
					sample.velocity.angularVelocity = mTrackingFromStanding->orientation * angularVelocity;
				}
				result[side] = sample;
			}
		}
		return result;
	}
} // namespace HOL::SteamVR
