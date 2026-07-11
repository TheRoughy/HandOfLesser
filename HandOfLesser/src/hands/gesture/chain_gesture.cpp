#include "proximity_gesture.h"

#include <Eigen/Core>
#include <Eigen/Geometry>
#include "src/openxr/xr_joint_utils.h"
#include "src/core/ui/user_interface.h"
#include "chain_gesture.h"
#include "src/util/hol_utils.h"

using namespace HOL::OpenXR;

namespace HOL::Gesture
{
	float ChainGesture::Gesture::evaluateInternal(GestureData data)
	{
		if (this->mChainedGestures.empty())
		{
			return 0.0f;
		}

		if (this->mCurrentGestureIndex >= this->mChainedGestures.size())
		{
			this->mCurrentGestureIndex = 0;
			this->mActivated = false;
			this->mCurrentGesturePressed = false;
			this->mRequireCurrentGestureRelease = false;
			return 0.0f;
		}

		auto currGesture = this->mChainedGestures[this->mCurrentGestureIndex];
		float currentGestureValue = currGesture->evaluate(data);
		const bool currentGestureActive = currentGestureValue >= 1.0f;

		if (this->mRequireCurrentGestureRelease)
		{
			if (currentGestureActive)
			{
				this->mCurrentGesturePressed = true;
				return 0.0f;
			}

			this->mRequireCurrentGestureRelease = false;
			this->mCurrentGesturePressed = false;
		}

		const bool currentGestureOnDown = currentGestureActive && !this->mCurrentGesturePressed;
		this->mCurrentGesturePressed = currentGestureActive;

		if (!mActivated && mCurrentGestureIndex > 0
			&& timeSince(this->mLastActivation) > this->parameters.maxDelay)
		{
			this->mCurrentGestureIndex = 0;
			this->mActivated = false;
			this->mCurrentGesturePressed = false;
			this->mRequireCurrentGestureRelease = false;
			return 0.0f;
		}

		if (this->mCurrentGestureIndex == this->mChainedGestures.size() - 1)
		{
			if (currentGestureOnDown)
			{
				this->mActivated = true;
				return currentGestureValue;
			}

			if (currentGestureActive && this->mActivated)
			{
				return currentGestureValue;
			}

			if (!currentGestureActive && this->mActivated)
			{
				this->mCurrentGestureIndex = 0;
				this->mActivated = false;
				this->mCurrentGesturePressed = false;
				this->mRequireCurrentGestureRelease = false;
				return 0.0f;
			}
		}
		else if (currentGestureOnDown)
		{
			this->mCurrentGestureIndex++;
			this->mLastActivation = std::chrono::steady_clock::now();
			this->mCurrentGesturePressed = false;
			this->mRequireCurrentGestureRelease = true;
			return 0.0f;
		}

		return 0.0f;
	}

	void ChainGesture::Gesture::addGesture(std::shared_ptr<BaseGesture::Gesture> gesture)
	{
		this->mChainedGestures.push_back(gesture);
		this->mSubGestures.push_back(gesture);
	}

}
