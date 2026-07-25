#include "gate_gesture.h"

#include <algorithm>

namespace HOL::Gesture::GateGesture
{
	void Gesture::setTriggerGesture(std::shared_ptr<BaseGesture::Gesture> gesture)
	{
		this->mTriggerGesture = gesture;
		this->mSubGestures.clear();
		if (this->mTriggerGesture)
		{
			this->mSubGestures.push_back(this->mTriggerGesture);
		}
		if (this->mModifierGesture)
		{
			this->mSubGestures.push_back(this->mModifierGesture);
		}
	}

	void Gesture::setModifierGesture(std::shared_ptr<BaseGesture::Gesture> gesture)
	{
		this->mModifierGesture = gesture;
		this->mSubGestures.clear();
		if (this->mTriggerGesture)
		{
			this->mSubGestures.push_back(this->mTriggerGesture);
		}
		if (this->mModifierGesture)
		{
			this->mSubGestures.push_back(this->mModifierGesture);
		}
	}

	float Gesture::evaluateInternal(GestureData data)
	{
		if (!this->mTriggerGesture)
		{
			return 0.0f;
		}

		float triggerValue = this->mTriggerGesture->evaluate(data);
		if (!this->mModifierGesture)
		{
			return triggerValue;
		}

		auto now = std::chrono::steady_clock::now();
		float modifierValue = std::clamp(this->mModifierGesture->evaluate(data), 0.0f, 1.0f);
		bool triggerActive = triggerValue >= 1.0f;
		bool modifierActive = modifierValue >= 1.0f;

		if (triggerActive && !this->mTriggerWasActive)
		{
			this->mTriggerActiveSince = now;
			this->mTriggerWasActive = true;
		}

		if (!triggerActive)
		{
			// The gate override only protects an already-triggered main gesture. Once the main
			// gesture drops, use the live modifier value again and allow a fresh attempt.
			this->mGateLocked = false;
			this->mBlockedUntilReleased = false;
			this->mTriggerWasActive = false;
			this->mQualificationActive = false;
		}

		if (modifierActive)
		{
			if (!this->mModifierWasActive)
			{
				this->mModifierActiveSince = now;
				this->mModifierWasActive = true;
			}
		}
		else
		{
			this->mModifierWasActive = false;
			this->mQualificationActive = false;
		}

		if (this->mGateLocked)
		{
			return triggerValue;
		}

		if (this->mBlockedUntilReleased)
		{
			return 0.0f;
		}

		if (!triggerActive)
		{
			return modifierActive ? triggerValue : 0.0f;
		}

		if (!modifierActive)
		{
			if ((now - this->mTriggerActiveSince) > this->parameters.allowedLagTime)
			{
				this->mBlockedUntilReleased = true;
			}
			return 0.0f;
		}

		bool modifierWasActiveBeforeTrigger
			= this->mModifierActiveSince <= this->mTriggerActiveSince;
		bool modifierActivatedWithinLag
			= (this->mModifierActiveSince - this->mTriggerActiveSince)
			  <= this->parameters.allowedLagTime;

		if (!modifierWasActiveBeforeTrigger && !modifierActivatedWithinLag)
		{
			this->mBlockedUntilReleased = true;
			return 0.0f;
		}

		if (!this->mQualificationActive)
		{
			this->mQualificationStartTime = now;
			this->mQualificationActive = true;
		}

		if ((now - this->mQualificationStartTime) < this->parameters.requiredActiveTime)
		{
			return 0.0f;
		}

		this->mGateLocked = true;
		return triggerValue;
	}
} // namespace HOL::Gesture::GateGesture
