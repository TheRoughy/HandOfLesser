#pragma once

#include "base_gesture.h"
#include <chrono>

namespace HOL::Gesture::GateGesture
{
	struct Parameters
	{
		std::chrono::milliseconds allowedLagTime{0};
		std::chrono::milliseconds requiredActiveTime{0};
	};

	class Gesture : public BaseGesture::Gesture
	{
	public:
		Gesture()
		{
			this->name = "GateGesture";
		}

		static std::shared_ptr<Gesture> Create()
		{
			return std::make_shared<Gesture>();
		}

		void setTriggerGesture(std::shared_ptr<BaseGesture::Gesture> gesture);
		void setModifierGesture(std::shared_ptr<BaseGesture::Gesture> gesture);

		Parameters parameters;

	protected:
		float evaluateInternal(GestureData data) override;

	private:
		std::shared_ptr<BaseGesture::Gesture> mTriggerGesture;
		std::shared_ptr<BaseGesture::Gesture> mModifierGesture;
		bool mGateLocked = false;
		bool mBlockedUntilReleased = false;
		bool mTriggerWasActive = false;
		bool mModifierWasActive = false;
		bool mQualificationActive = false;
		std::chrono::steady_clock::time_point mTriggerActiveSince;
		std::chrono::steady_clock::time_point mModifierActiveSince;
		std::chrono::steady_clock::time_point mQualificationStartTime;
	};
} // namespace HOL::Gesture::GateGesture
