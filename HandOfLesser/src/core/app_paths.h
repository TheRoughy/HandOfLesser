#pragma once

#include <filesystem>
#include <iostream>
#include <windows.h>
#include <shlobj.h>

namespace HOL::Paths
{
	inline std::filesystem::path getAppDataDirectory()
	{
		PWSTR localAppDataPath = nullptr;
		HRESULT result
			= SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &localAppDataPath);
		if (FAILED(result) || localAppDataPath == nullptr)
		{
			std::cerr << "Failed to resolve LocalAppData, falling back to current directory."
					  << std::endl;
			return std::filesystem::current_path();
		}

		std::filesystem::path appDataDirectory(localAppDataPath);
		CoTaskMemFree(localAppDataPath);

		appDataDirectory /= "TheRoughy";
		appDataDirectory /= "HandOfLesser";
		return appDataDirectory;
	}

	inline std::filesystem::path ensureAppDataDirectory()
	{
		std::filesystem::path appDataDirectory = getAppDataDirectory();
		std::error_code errorCode;
		std::filesystem::create_directories(appDataDirectory, errorCode);
		if (errorCode)
		{
			std::cerr << "Failed to create app data directory '" << appDataDirectory.string()
					  << "', falling back to current directory." << std::endl;
			return std::filesystem::current_path();
		}

		return appDataDirectory;
	}

	inline std::filesystem::path getSettingsFilePath()
	{
		return ensureAppDataDirectory() / "settings.json";
	}

	inline std::filesystem::path getImguiIniFilePath()
	{
		return ensureAppDataDirectory() / "imgui.ini";
	}

	inline std::filesystem::path getVisualizerSnapshotsFilePath()
	{
		return ensureAppDataDirectory() / "visualizer_snapshots.json";
	}

	inline std::filesystem::path getCrashDirectory()
	{
		return ensureAppDataDirectory() / "crashes";
	}

	inline std::filesystem::path getLogFilePath()
	{
		return ensureAppDataDirectory() / "HandOfLesser.log";
	}
} // namespace HOL::Paths
