#pragma once

#include "base_gesture.h"
#include <HandOfLesserCommon.h>

namespace HOL::Gesture::AboveBelowCurlPlaneGesture
{
	struct Parameters
	{
		FingerType planeFinger;
		FingerType otherFinger;
		HandSide side;
		float minimumDistanceAbovePlane = 0.0f;
	};

	class Gesture : public BaseGesture::Gesture
	{

	public:
		Gesture() : BaseGesture::Gesture()
		{
			this->name = "AboveBelowCurlPlaneGesture";
		};
		static std::shared_ptr<Gesture> Create()
		{
			return std::make_shared<Gesture>();
		}

		AboveBelowCurlPlaneGesture::Parameters parameters;

	private:

	protected:
		float evaluateInternal(GestureData data) override;
	};
} // namespace HOL::Gesture::AboveBelowCurlPlaneGesture
