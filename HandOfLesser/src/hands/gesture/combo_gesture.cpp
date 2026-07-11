#include "proximity_gesture.h"

#include <algorithm>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include "src/openxr/xr_joint_utils.h"
#include "src/core/ui/user_interface.h"
#include "chain_gesture.h"
#include "src/util/hol_utils.h"
#include "combo_gesture.h"

using namespace HOL;
using namespace HOL::OpenXR;

namespace HOL::Gesture
{
	float ComboGesture::Gesture::evaluateInternal(GestureData data)
	{
		float min = 1;
		float max = 0;
		float product = 1;
		for (auto& gesture : this->mComboGestures)
		{
			float val = gesture.get()->evaluate(data);
			if (val < min)
				min = val;
			if (val > max)
				max = val;
			product *= std::clamp(val, 0.0f, 1.0f);
		}

		float value = this->parameters.valueMode == ComboGesture::ValueMode::Product ? product : min;

		if (value >= 1)
		{
			this->mActivated = true;
			return 1;
		}

		if (this->parameters.holdUntilAllReleased && this->mActivated && max >= 1.f)
		{
			return 1;
		}
		else
		{
			this->mActivated = false;
		}

		return value;
	}

	void ComboGesture::Gesture::addGesture(std::shared_ptr<BaseGesture::Gesture> gesture)
	{
		this->mComboGestures.push_back(gesture);
		this->mSubGestures.push_back(gesture);
	}
}
