#include "proximity_gesture.h"

#include <Eigen/Core>
#include <Eigen/Geometry>
#include "src/openxr/xr_joint_utils.h"
#include "above_below_curl_plane_gesture.h"
#include "src/core/ui/user_interface.h"

using namespace HOL::OpenXR;

namespace HOL::Gesture
{
	float AboveBelowCurlPlaneGesture::Gesture::evaluateInternal(GestureData data)
	{
		// The plane is defined by the X axis of the palm, and the vector between the knucle
		// and the tip of the finger.
		auto planeKnuckleJoint
			= getJointPosition(data.joints[this->parameters.side],
							   getFirstFingerJoint(this->parameters.planeFinger));
		auto planeTipJoint = getJointPosition(data.joints[this->parameters.side],
							   (XrHandJointEXT)(getFingerTip(this->parameters.planeFinger)));

		auto palmOrientation = getJointOrientation(data.joints[this->parameters.side],
												   XrHandJointEXT::XR_HAND_JOINT_PALM_EXT);

		// Palm X and knucke-to-tip crossed to get plane orientation
		Eigen::Vector3f palmX = palmOrientation * Eigen::Vector3f(1, 0, 0);

		Eigen::Vector3f planeNormal = palmX.cross(planeTipJoint - planeKnuckleJoint);
		planeNormal.normalize();

		// Dot product sign decides whether the other fingertip is above or below the plane.
		auto otherTipJoint = getJointPosition(data.joints[this->parameters.side],
											  getFingerTip(this->parameters.otherFinger));

		Eigen::Vector3f otherVector = otherTipJoint - planeTipJoint;

		return planeNormal.dot(otherVector) > 0.0f ? 1.0f : 0.0f;
	}
}

