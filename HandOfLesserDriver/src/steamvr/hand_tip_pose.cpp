#include "hand_tip_pose.h"

#include <algorithm>
#include <cmath>

namespace HOL::SteamVR
{
	Eigen::Quaternionf HandTipPoseGenerator::makeTipOrientation(const Eigen::Vector3f& direction,
																const Eigen::Vector3f& preferredUp)
	{
		const Eigen::Vector3f forward = direction.normalized();
		// Remove the part of "up" that points forward so it only controls pointer roll.
		Eigen::Vector3f up = preferredUp - forward * preferredUp.dot(forward);
		// A parallel up vector cannot define roll. Fall back to a world axis that can.
		if (up.squaredNorm() < DirectionEpsilonSquared)
		{
			up = Eigen::Vector3f::UnitY() - forward * forward.y();
		}
		if (up.squaredNorm() < DirectionEpsilonSquared)
		{
			up = Eigen::Vector3f::UnitX() - forward * forward.x();
		}
		up.normalize();

		// Build three perpendicular local axes from forward and up. SteamVR tip poses point along
		// local -Z, while local +Y defines the pointer's roll.
		const Eigen::Vector3f localZ = -forward;
		const Eigen::Vector3f localX = up.cross(localZ).normalized();
		// Rebuild local Y from X and Z so all three axes are exactly perpendicular.
		const Eigen::Vector3f localY = localZ.cross(localX).normalized();
		Eigen::Matrix3f rotation;
		rotation.col(0) = localX;
		rotation.col(1) = localY;
		rotation.col(2) = localZ;
		return Eigen::Quaternionf(rotation).normalized();
	}

	Eigen::Quaternionf
	HandTipPoseGenerator::stabilizeOrientation(const Eigen::Quaternionf& orientation,
											   float smoothingMS)
	{
		const auto now = std::chrono::steady_clock::now();
		if (!mHasOrientation || smoothingMS <= 0.0f || mLastUpdateTime >= now)
		{
			mStabilizedOrientation = orientation;
			mHasOrientation = true;
		}
		else
		{
			const float elapsedSeconds
				= std::chrono::duration<float>(now - mLastUpdateTime).count();
			const float smoothingSeconds = smoothingMS / 1000.0f;
			const float alpha = 1.0f - std::exp(-elapsedSeconds / smoothingSeconds);
			mStabilizedOrientation
				= mStabilizedOrientation.slerp(std::clamp(alpha, 0.0f, 1.0f), orientation)
					  .normalized();
		}
		mLastUpdateTime = now;
		return mStabilizedOrientation;
	}

	std::optional<vr::HmdMatrix34_t>
	HandTipPoseGenerator::generate(HOL::HandSide side,
								   const HOL::PoseLocation& palmPose,
								   const vr::DriverPose_t& controllerPose,
								   const vr::DriverPose_t& hmdPose,
								   float stabilizationSmoothingMS)
	{
		if (side < HOL::HandSide::LeftHand || side >= HOL::HandSide::HandSide_MAX)
		{
			return std::nullopt;
		}

		TipCalibration calibration = LeftHandCalibration;
		if (side == HOL::HandSide::RightHand)
		{
			// Flip offsets and stuff depending on hand
			calibration.palmLocalOffset = HOL::flipHandTranslation(calibration.palmLocalOffset);
			calibration.controllerLocalAimDirection
				= HOL::flipHandTranslation(calibration.controllerLocalAimDirection);
			calibration.hmdLocalAimDirection
				= HOL::flipHandTranslation(calibration.hmdLocalAimDirection);
			calibration.hmdLocalRotationOffsetDegrees
				= HOL::flipHandRotation(calibration.hmdLocalRotationOffsetDegrees);
		}

		// Resolve SteamVR's tracking and driver transforms so every pose uses the same world space.
		const HOL::PoseLocation controllerWorld = getSteamVRDevicePose(controllerPose);
		const HOL::PoseLocation hmdWorld = getSteamVRDevicePose(hmdPose);
		const Eigen::Quaternionf worldToHmd = hmdWorld.orientation.inverse();
		// Position drives most of the aim. Reducing the vertical component prevents the pointer
		// from pitching too aggressively as the hand moves above or below the headset.
		Eigen::Vector3f hmdLocalHeadToPalm = worldToHmd * (palmPose.position - hmdWorld.position);
		hmdLocalHeadToPalm.y() *= HeadToPalmVerticalScale;
		if (hmdLocalHeadToPalm.squaredNorm() < DirectionEpsilonSquared)
		{
			return std::nullopt;
		}

		const Eigen::Vector3f headToPalmAim
			= hmdWorld.orientation * hmdLocalHeadToPalm.normalized();
		const Eigen::Vector3f controllerTipAim
			= controllerWorld.orientation * calibration.controllerLocalAimDirection.normalized();
		const Eigen::Vector3f hmdTipAim
			= hmdWorld.orientation * calibration.hmdLocalAimDirection.normalized();
		// Point mostly based on where the hand is relative to the head. Controller rotation lets
		// the wrist steer it, while the HMD-local direction keeps the result stable.
		const Eigen::Vector3f forwardDirection = headToPalmAim * HeadToPalmAimWeight
												 + controllerTipAim * ControllerTipAimWeight
												 + hmdTipAim * HmdTipAimWeight;

		const Eigen::Vector3f hmdUp = hmdWorld.orientation * Eigen::Vector3f::UnitY();
		const Eigen::Vector3f controllerRollReference
			= controllerWorld.orientation * Eigen::Vector3f::UnitX();
		// Start from headset up for stability, then turn most of the way toward the controller's
		// roll.
		const Eigen::Vector3f pointerUp
			= HOL::slerpDirections(hmdUp, controllerRollReference, ControllerRollInfluence);
		if (forwardDirection.squaredNorm() < DirectionEpsilonSquared
			|| pointerUp.squaredNorm() < DirectionEpsilonSquared)
		{
			return std::nullopt;
		}

		HOL::PoseLocation worldPose;
		worldPose.position = palmPose.position + palmPose.orientation * calibration.palmLocalOffset;
		// Convert the HMD-local adjustment to world space and apply it to the complete pointer
		// orientation, allowing independent pitch, yaw, and roll corrections.
		const Eigen::Quaternionf hmdLocalRotationOffset
			= HOL::quaternionFromEulerAnglesDegrees(calibration.hmdLocalRotationOffsetDegrees);
		const Eigen::Quaternionf worldRotationOffset
			= hmdWorld.orientation * hmdLocalRotationOffset * worldToHmd;
		worldPose.orientation
			= worldRotationOffset * makeTipOrientation(forwardDirection, pointerUp);
		// Palm filtering already stabilizes tip position. Filter the final world orientation here
		// as it also depends on the independently moving HMD pose.
		worldPose.orientation
			= stabilizeOrientation(worldPose.orientation, stabilizationSmoothingMS);
		// SteamVR expects /pose/tip relative to the controller rather than in world space.
		const HOL::PoseLocation localPose = getRelativePose(controllerWorld, worldPose);
		return poseLocationToMatrix34(localPose);
	}
} // namespace HOL::SteamVR
