#pragma once

#include <Eigen/Core>
#include <Eigen/Geometry>

namespace HOL
{

	Eigen::Quaternionf quaternionFromEulerAngles(Eigen::Vector3f);
	Eigen::Quaternionf quaternionFromEulerAnglesDegrees(Eigen::Vector3f degrees);
	Eigen::Quaternionf quaternionFromEulerAnglesDegrees(float x, float y, float z);
	Eigen::Quaternionf quaternionFromEulerAngles(float x, float y, float z);
	Eigen::Quaternionf quaternionFromEulerAnglesZYXDegrees(float x, float y, float z);
	Eigen::Vector3f quaternionToEulerAngles(const Eigen::Quaternionf& q);
	float degreesToRadians(float degrees);
	float radiansToDegrees(float radians);
	float angleBetweenVectors(const Eigen::Vector3f& first, const Eigen::Vector3f& second);
	// Interpolate around the angle between two directions without shortening the result.
	Eigen::Vector3f
	slerpDirections(const Eigen::Vector3f& from, const Eigen::Vector3f& to, float alpha);
	float getClosestSegmentDistance(const Eigen::Vector3f& p1,
									const Eigen::Vector3f& q1,
									const Eigen::Vector3f& p2,
									const Eigen::Vector3f& q2);

	Eigen::Vector3f flipHandRotation(Eigen::Vector3f& rot);
	Eigen::Vector3f flipHandTranslation(Eigen::Vector3f& trans);

	Eigen::Vector3f
	translateLocal(Eigen::Vector3f position, Eigen::Quaternionf axis, Eigen::Vector3f offset);
	Eigen::Quaternionf rotateLocal(Eigen::Quaternionf axis, Eigen::Quaternionf offset);

} // namespace HOL
