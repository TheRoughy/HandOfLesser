#pragma once

#include <d3d11.h>
#include <openxr/openxr_platform.h>
#include "openxr/openxr_structs.hpp"
#include <windows.h>
#include <iostream>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <vector>
#include <HandOfLesserCommon.h>

namespace HOL::OpenXR
{
	bool hasExtension(std::vector<xr::ExtensionProperties> const& extensions, const char* name);

	template <typename Dispatch> xr::Time getXrTimeNow(xr::Instance instance, Dispatch&& d)
	{
		LARGE_INTEGER qpc;
		QueryPerformanceCounter(&qpc);
		return instance.convertWin32PerformanceCounterToTimeKHR(&qpc, d);
	}

	template <typename Dispatch> void pollEvent(xr::Instance instance, Dispatch&& d)
	{
		while (1)
		{
			xr::EventDataBuffer event;
			auto result = instance.pollEvent(event, d);
			if (result == xr::Result::EventUnavailable)
			{
				return;
			}
			else if (result != xr::Result::Success)
			{
				std::cout << "Got error polling for events: " << to_string(result) << std::endl;
				return;
			}

			std::cout << "Event: " << to_string_literal(event.type) << std::endl;
			if (event.type == xr::StructureType::EventDataSessionStateChanged)
			{
				auto& change = reinterpret_cast<xr::EventDataSessionStateChanged&>(event);
				std::cout << to_string(change.state) << std::endl;
			}
		}
	}

	bool handleXR(std::string what, XrResult res);

	Eigen::Vector3f toEigenVector(const XrVector3f& xrVector);
	XrVector3f toXrVector(const Eigen::Vector3f& eigenVector);

	Eigen::Quaternionf toEigenQuaternion(const XrQuaternionf& xrQuat);

	XrQuaternionf toXrQuaternion(const Eigen::Quaternionf& eigenQuat);

	XrHandEXT toOpenXRHandSide(HOL::HandSide side);

	std::string getActiveOpenXRRuntimePath(int majorApiVersion);
	std::string getActiveOpenXRRuntimeName(int majorApiVersion);
	std::string getOpenXRRuntimeName(std::string runtimePath);
	std::vector<std::string> getAvailableOpenXRRuntimePaths(int majorApiVersion);
	bool setOpenXRRuntimeOverride(const std::string& runtimePath);
} // namespace HOL::OpenXR
