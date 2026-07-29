#pragma once

#include "base_gesture.h"

namespace HOL::Gesture::FacingGesture
{
	enum class Source
	{
		Head,
		Palm
	};

	enum class Target
	{
		HandPalm,
		Head
	};

	struct Parameters
	{
		HOL::HandSide side = HOL::LeftHand;
		float fovDegrees = 45.0f;
		Source source = Source::Head;
		Target target = Target::HandPalm;
		Eigen::Vector3f localForward = Eigen::Vector3f::UnitY();
	};

	class Gesture : public BaseGesture::Gesture
	{
	public:
		Gesture()
		{
			this->name = "FacingGesture";
		}

		static std::shared_ptr<Gesture> Create()
		{
			return std::make_shared<Gesture>();
		}

		Parameters parameters;

	protected:
		float evaluateInternal(GestureData data) override;
	};
} // namespace HOL::Gesture::FacingGesture
