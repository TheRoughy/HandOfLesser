#include "proximity_gesture.h"

#include <Eigen/Core>
#include <Eigen/Geometry>
#include "src/core/settings_global.h"
#include "src/openxr/xr_joint_utils.h"
#include "above_below_curl_plane_gesture.h"
#include "combo_gesture.h"
#include "open_hand_pinch_gesture.h"

using namespace HOL;
using namespace HOL::OpenXR;

namespace HOL::Gesture::OpenHandPinchGesture
{
	float Gesture::evaluateInternal(GestureData data)
	{
		return this->mGateGesture ? this->mGateGesture->evaluate(data) : 0.0f;
	}

	void Gesture::setup()
	{
		this->name = "OpenHandPinchGesture";
		this->mSubGestures.clear();

		////////////////////
		// Pinch proximity
		////////////////////

		this->mProxGesture = ProximityGesture::Create();
		this->mProxGesture->setup(this->parameters.pinchFinger,
								  this->parameters.side,
								  FingerThumb,
								  this->parameters.side,
								  HOL::Config.input.pinchDistanceMM / 1000.0f);

		/////////////////////////////////////
		// Other fingers above pinch plane
		/////////////////////////////////////

		std::vector<HOL::FingerType> allPinchFingers = {FingerType::FingerIndex,
														FingerType::FingerMiddle,
														FingerType::FingerRing,
														FingerType::FingerLittle};

		for (auto otherFinger : allPinchFingers)
		{
			// Don't check the finger we are pinching
			if (otherFinger == this->parameters.pinchFinger)
				continue;

			auto gesture = AboveBelowCurlPlaneGesture::Gesture::Create();
			gesture->parameters.otherFinger = otherFinger;
			gesture->parameters.planeFinger = this->parameters.pinchFinger;
			gesture->parameters.side = this->parameters.side;

			this->mCurlPlaneGestures.push_back(gesture);
		}

		auto abovePlaneGesture = ComboGesture::Gesture::Create();
		abovePlaneGesture->parameters.holdUntilAllReleased = false;
		abovePlaneGesture->parameters.valueMode = ComboGesture::ValueMode::Minimum;
		for (auto& gesture : this->mCurlPlaneGestures)
		{
			abovePlaneGesture->addGesture(gesture);
		}

		this->mGateGesture = GateGesture::Gesture::Create();
		this->mGateGesture->parameters.allowedLagTime
			= std::chrono::milliseconds(HOL::Config.input.gateLagTimeMS);
		this->mGateGesture->setTriggerGesture(this->mProxGesture);
		this->mGateGesture->setModifierGesture(abovePlaneGesture);

		this->mSubGestures.clear();
		this->mSubGestures.push_back(this->mGateGesture);
	}
}

