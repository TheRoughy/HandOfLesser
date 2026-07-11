#include "XrUtils.h"
#include <src/windows/windows_utils.h>
#include <algorithm>

namespace HOL::OpenXR
{
	bool hasExtension(std::vector<xr::ExtensionProperties> const& extensions, const char* name)
	{
		auto b = extensions.begin();
		auto e = extensions.end();

		const std::string name_string = name;

		return (e
				!= std::find_if(b,
								e,
								[&](const xr::ExtensionProperties& prop)
								{ return prop.extensionName == name_string; }));
	}

	bool handleXR(std::string what, XrResult res)
	{
		if (res != XrResult::XR_SUCCESS)
		{
			std::cout << what << " failed with " << res << std::endl;
			return false;
		}

		return true;
	}

	Eigen::Vector3f toEigenVector(const XrVector3f& xrVector)
	{
		return Eigen::Vector3f(xrVector.x, xrVector.y, xrVector.z);
	}

	XrVector3f toXrVector(const Eigen::Vector3f& eigenVector)
	{
		XrVector3f xrVector;
		xrVector.x = eigenVector.x();
		xrVector.y = eigenVector.y();
		xrVector.z = eigenVector.z();
		return xrVector;
	}

	Eigen::Quaternionf toEigenQuaternion(const XrQuaternionf& xrQuat)
	{
		return Eigen::Quaternionf(xrQuat.w, xrQuat.x, xrQuat.y, xrQuat.z);
	}

	XrQuaternionf toXrQuaternion(const Eigen::Quaternionf& eigenQuat)
	{
		XrQuaternionf xrQuat;
		xrQuat.w = eigenQuat.w();
		xrQuat.x = eigenQuat.x();
		xrQuat.y = eigenQuat.y();
		xrQuat.z = eigenQuat.z();
		return xrQuat;
	}

	XrHandEXT toOpenXRHandSide(HOL::HandSide side)
	{
		switch (side)
		{
			case HOL::LeftHand:
				return XrHandEXT::XR_HAND_LEFT_EXT;
			case HOL::RightHand:
				return XrHandEXT::XR_HAND_RIGHT_EXT;
			default:
				return XrHandEXT::XR_HAND_MAX_ENUM_EXT; // Just don't do this
		}
	}

	std::string getActiveOpenXRRuntimePath(int majorApiVersion)
	{
		char envBuffer[2048] = {};
		DWORD envLength = GetEnvironmentVariableA("XR_RUNTIME_JSON", envBuffer, sizeof(envBuffer));
		if (envLength > 0 && envLength < sizeof(envBuffer))
		{
			return std::string(envBuffer, envLength);
		}

		std::string runtimePath = "Unknown";
		std::string path = "SOFTWARE\\Khronos\\OpenXR\\" + std::to_string(majorApiVersion);

		RegGetString(HKEY_LOCAL_MACHINE, path, "ActiveRuntime", runtimePath);

		return runtimePath;
	}

	std::string getOpenXRRuntimeName(std::string runtimePath)
	{
		// In an ideal world we'd go read the json, but I am lazy
		size_t lastSlash = runtimePath.find_last_of('\\');
		if (lastSlash != std::string::npos && lastSlash > 0)
		{
			// Just get filename from path
			runtimePath = runtimePath.substr(lastSlash + 1);
		}

		return runtimePath;
	}

	std::string getActiveOpenXRRuntimeName(int majorApiVersion)
	{
		return getOpenXRRuntimeName(getActiveOpenXRRuntimePath(majorApiVersion));
	}

	std::vector<std::string> getAvailableOpenXRRuntimePaths(int majorApiVersion)
	{
		std::vector<std::string> runtimePaths;
		std::string path = "SOFTWARE\\Khronos\\OpenXR\\" + std::to_string(majorApiVersion)
						   + "\\AvailableRuntimes";

		HKEY hKey = nullptr;
		LONG result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, path.c_str(), 0, KEY_READ, &hKey);
		if (result != ERROR_SUCCESS)
		{
			return runtimePaths;
		}

		DWORD valueCount = 0;
		DWORD maxValueNameLength = 0;
		result = RegQueryInfoKeyA(hKey,
								  nullptr,
								  nullptr,
								  nullptr,
								  nullptr,
								  nullptr,
								  nullptr,
								  &valueCount,
								  &maxValueNameLength,
								  nullptr,
								  nullptr,
								  nullptr);
		if (result != ERROR_SUCCESS)
		{
			RegCloseKey(hKey);
			return runtimePaths;
		}

		std::vector<char> valueName(maxValueNameLength + 1);
		for (DWORD i = 0; i < valueCount; i++)
		{
			DWORD valueNameLength = (DWORD)valueName.size();
			DWORD valueType = 0;
			DWORD valueData = 0;
			DWORD valueDataSize = sizeof(valueData);
			result = RegEnumValueA(hKey,
								   i,
								   valueName.data(),
								   &valueNameLength,
								   nullptr,
								   &valueType,
								   reinterpret_cast<LPBYTE>(&valueData),
								   &valueDataSize);
			if (result != ERROR_SUCCESS || valueType != REG_DWORD || valueData != 0)
			{
				continue;
			}

			std::string runtimePath(valueName.data(), valueNameLength);
			DWORD attributes = GetFileAttributesA(runtimePath.c_str());
			if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
			{
				std::cout << "Skipping missing OpenXR runtime manifest: " << runtimePath
						  << std::endl;
				continue;
			}

			runtimePaths.emplace_back(runtimePath);
		}

		RegCloseKey(hKey);

		std::sort(runtimePaths.begin(), runtimePaths.end());
		runtimePaths.erase(std::unique(runtimePaths.begin(), runtimePaths.end()),
						   runtimePaths.end());
		return runtimePaths;
	}

	bool setOpenXRRuntimeOverride(const std::string& runtimePath)
	{
		LPCSTR value = runtimePath.empty() ? nullptr : runtimePath.c_str();
		return SetEnvironmentVariableA("XR_RUNTIME_JSON", value) != 0;
	}

} // namespace HOL::OpenXR
