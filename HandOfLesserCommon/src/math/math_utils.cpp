#include "math_utils.h"
#include <algorithm>
#include <numbers> 


namespace HOL
{
	Eigen::Quaternionf quaternionFromEulerAngles(Eigen::Vector3f euler)
	{
		return quaternionFromEulerAngles(euler.x(), euler.y(), euler.z());
	}

	Eigen::Quaternionf quaternionFromEulerAngles(float x, float y, float z)
	{
		return Eigen::Quaternionf(Eigen::AngleAxisf(x, Eigen::Vector3f::UnitX())
								  * Eigen::AngleAxisf(y, Eigen::Vector3f::UnitY())
								  * Eigen::AngleAxisf(z, Eigen::Vector3f::UnitZ()));
	}

	Eigen::Quaternionf quaternionFromEulerAnglesZYXDegrees(float x, float y, float z) 
	{
		 return Eigen::Quaternionf(Eigen::AngleAxisf(degreesToRadians(z), Eigen::Vector3f::UnitZ())
		 						  * Eigen::AngleAxisf(degreesToRadians(y), Eigen::Vector3f::UnitY())
		 						  * Eigen::AngleAxisf(degreesToRadians(x), Eigen::Vector3f::UnitX()));
	} 

	Eigen::Quaternionf quaternionFromEulerAnglesDegrees(float x, float y, float z)
	{
		return quaternionFromEulerAngles(
			degreesToRadians(x), degreesToRadians(y), degreesToRadians(z));
	}

	Eigen::Quaternionf quaternionFromEulerAnglesDegrees(Eigen::Vector3f degrees)
	{
		return quaternionFromEulerAnglesDegrees(degrees.x(), degrees.y(), degrees.z());
	}

	Eigen::Vector3f quaternionToEulerAngles(const Eigen::Quaternionf& q)
	{
		Eigen::Matrix3f rotationMatrix = q.toRotationMatrix();
		return rotationMatrix.canonicalEulerAngles(0, 1, 2); // XYZ
	}

	float degreesToRadians(float degrees)
	{
		return degrees * (std::numbers::pi_v<float> / 180.0f);
	}

	float radiansToDegrees(float radians)
	{
		return radians * (180.0f / std::numbers::pi_v<float>);
	}
	float angleBetweenVectors(const Eigen::Vector3f& first, const Eigen::Vector3f& second)
	{
		return std::atan2(first.cross(second).norm(), first.dot(second));
	}

	Eigen::Vector3f
	slerpDirections(const Eigen::Vector3f& from, const Eigen::Vector3f& to, float alpha)
	{
		const Eigen::Vector3f normalizedFrom = from.normalized();
		const Eigen::Vector3f normalizedTo = to.normalized();
		const Eigen::Quaternionf fullRotation
			= Eigen::Quaternionf::FromTwoVectors(normalizedFrom, normalizedTo);
		return Eigen::Quaternionf::Identity().slerp(alpha, fullRotation) * normalizedFrom;
	}

	float getClosestSegmentDistance(const Eigen::Vector3f& p1,
								   const Eigen::Vector3f& q1,
								   const Eigen::Vector3f& p2,
								   const Eigen::Vector3f& q2)
	{
		constexpr float Epsilon = 1e-6f;

		Eigen::Vector3f d1 = q1 - p1;
		Eigen::Vector3f d2 = q2 - p2;
		Eigen::Vector3f r = p1 - p2;
		float a = d1.dot(d1);
		float e = d2.dot(d2);
		float f = d2.dot(r);

		float s = 0.0f;
		float t = 0.0f;

		if (a <= Epsilon && e <= Epsilon)
		{
			return (p1 - p2).norm();
		}

		if (a <= Epsilon)
		{
			t = std::clamp(f / e, 0.0f, 1.0f);
		}
		else
		{
			float c = d1.dot(r);

			if (e <= Epsilon)
			{
				s = std::clamp(-c / a, 0.0f, 1.0f);
			}
			else
			{
				float b = d1.dot(d2);
				float denom = a * e - b * b;

				if (denom != 0.0f)
				{
					s = std::clamp((b * f - c * e) / denom, 0.0f, 1.0f);
				}

				t = (b * s + f) / e;

				if (t < 0.0f)
				{
					t = 0.0f;
					s = std::clamp(-c / a, 0.0f, 1.0f);
				}
				else if (t > 1.0f)
				{
					t = 1.0f;
					s = std::clamp((b - c) / a, 0.0f, 1.0f);
				}
			}
		}

		Eigen::Vector3f c1 = p1 + d1 * s;
		Eigen::Vector3f c2 = p2 + d2 * t;
		return (c1 - c2).norm();
	}

	Eigen::Vector3f flipHandRotation(Eigen::Vector3f& rot)
	{
		// left/right rotation needs to flip x and y?
		return Eigen::Vector3f(rot.x(), -rot.y(), -rot.z());
	}

	Eigen::Vector3f flipHandTranslation(Eigen::Vector3f& trans)
	{
		// Left/right translation only needs to flip z?
		return Eigen::Vector3f(-trans.x(), trans.y(), trans.z());
	}

	Eigen::Vector3f translateLocal(Eigen::Vector3f position, Eigen::Quaternionf axis, Eigen::Vector3f offset)
	{
		Eigen::Vector3f positionOffsetLocal = axis * offset;
		return position + positionOffsetLocal;
	}

	Eigen::Quaternionf rotateLocal(Eigen::Quaternionf axis, Eigen::Quaternionf offset)
	{
		return axis * offset;
	}

} // namespace HOL
