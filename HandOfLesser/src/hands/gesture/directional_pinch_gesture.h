#pragma once

#include "above_below_curl_plane_gesture.h"
#include "base_gesture.h"
#include "proximity_gesture.h"

#include <HandOfLesserCommon.h>

namespace HOL::Gesture::DirectionalPinchGesture
{
	struct Parameters
	{
		HOL::FingerType pinchFinger;
		HOL::HandSide side;
		float pinchDistance = 0.025f;
		float directionThreshold = 0.005f;
	};

	class Gesture : public BaseGesture::Gesture
	{
	public:
		Gesture()
		{
			this->name = "DirectionalPinchGesture";
		}

		static std::shared_ptr<Gesture> Create()
		{
			return std::make_shared<Gesture>();
		}

		void setup();

		Parameters parameters;

	protected:
		float evaluateInternal(GestureData data) override;

	private:
		std::shared_ptr<ProximityGesture> mProximityGesture;
		std::shared_ptr<AboveBelowCurlPlaneGesture::Gesture> mArmingDirectionGesture;
		std::shared_ptr<AboveBelowCurlPlaneGesture::Gesture> mDisarmingDirectionGesture;
		bool mArmed = false;
	};
} // namespace HOL::Gesture::DirectionalPinchGesture
