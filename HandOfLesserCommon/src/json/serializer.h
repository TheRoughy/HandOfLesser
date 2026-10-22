#pragma once

#include <nlohmann/json.hpp>
#include <Eigen/Core>
#include <Eigen/Geometry>

namespace nlohmann
{
	template <typename T>
	void get_to_if_present(const nlohmann::json& j, const std::string& key, T& value)
	{
		if (j.contains(key))
		{
			j.at(key).get_to(value);
		}
	}

	template <> struct adl_serializer<Eigen::Quaternionf>
	{
		static void to_json(json& j, const Eigen::Quaternionf& quaternion)
		{
			j = {{"x", quaternion.x()},
				 {"y", quaternion.y()},
				 {"z", quaternion.z()},
				 {"w", quaternion.w()}};
		}

		static void from_json(const json& j, Eigen::Quaternionf& quaternion)
		{
			quaternion = Eigen::Quaternionf(j.at("w").get<float>(),
										 j.at("x").get<float>(),
										 j.at("y").get<float>(),
										 j.at("z").get<float>());
		}
	};

	template <> struct adl_serializer<Eigen::Vector3f>
	{
		static void to_json(json& j, const Eigen::Vector3f& vec)
		{
			j = json{{"x", vec.x()}, {"y", vec.y()}, {"z", vec.z()}};
		}

		// Convert JSON to Eigen::Vector3f
		static void from_json(const json& j, Eigen::Vector3f& vec)
		{
			vec.x() = j.at("x").get<float>();
			vec.y() = j.at("y").get<float>();
			vec.z() = j.at("z").get<float>();
		}
	};
} // namespace nlohmann
