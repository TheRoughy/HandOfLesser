#pragma once

#include <chrono>
#include <optional>
#include <HandOfLesserCommon.h>

namespace HOL::SteamVR
{
	class HandTipPoseGenerator
	{
	public:
		std::optional<vr::HmdMatrix34_t> generate(HOL::HandSide side,
												  const HOL::PoseLocation& palmPose,
												  const vr::DriverPose_t& controllerPose,
												  const vr::DriverPose_t& hmdPose,
												  float stabilizationSmoothingMS);

	private:
		static constexpr float HeadToPalmVerticalScale = 0.58f;
		static constexpr float HeadToPalmAimWeight = 0.40f;
		static constexpr float ControllerTipAimWeight = 0.33f;
		static constexpr float HmdTipAimWeight = 0.27f;
		static constexpr float ControllerRollInfluence = 0.80f;
		static constexpr float DirectionEpsilonSquared = 1e-6f;

		struct TipCalibration
		{
			Eigen::Vector3f palmLocalOffset{0.0095f, -0.0810f, -0.0593f};
			Eigen::Vector3f controllerLocalAimDirection{0.42f, -0.87f, -0.27f};
			Eigen::Vector3f hmdLocalAimDirection{0.53f, 0.83f, -0.16f};
			Eigen::Vector3f hmdLocalRotationOffsetDegrees{0.0f, -10.0f, 0.0f};
		};

		// Flipped on the X axis when used for the right hand.
		inline static const TipCalibration LeftHandCalibration{};

		static Eigen::Quaternionf makeTipOrientation(const Eigen::Vector3f& direction,
													 const Eigen::Vector3f& preferredUp);
		Eigen::Quaternionf stabilizeOrientation(const Eigen::Quaternionf& orientation,
												float smoothingMS);

		Eigen::Quaternionf mStabilizedOrientation = Eigen::Quaternionf::Identity();
		std::chrono::steady_clock::time_point mLastUpdateTime{};
		bool mHasOrientation = false;
	};
} // namespace HOL::SteamVR
