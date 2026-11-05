#include "Settings.h"

#include <Windows.h>
#include <filesystem>
#include <string>
#include <vector>
#include <fstream>

#include "Unreal/UnrealObjects.h"
#include "Unreal/ObjectArray.h"

namespace
{
	std::ofstream DumpLog;
	std::streambuf* OriginalCerrBuffer = nullptr;
}

void Settings::Config::CloseLog()
{
	if (OriginalCerrBuffer)
	{
		std::cerr.flush();
		std::cerr.rdbuf(OriginalCerrBuffer);
		OriginalCerrBuffer = nullptr;
		DumpLog.close();
	}
}

void Settings::InitWeakObjectPtrSettings()
{
	const UEStruct LoadAsset = ObjectArray::FindObjectFast<UEFunction>("LoadAsset", EClassCastFlags::Function);

	if (!LoadAsset)
	{
		std::cerr << "\nDumper-7: 'LoadAsset' wasn't found, could not determine value for 'bIsWeakObjectPtrWithoutTag'!\n" << std::endl;
		return;
	}

	const UEProperty Asset = LoadAsset.FindMember("Asset", EClassCastFlags::SoftObjectProperty);
	if (!Asset)
	{
		std::cerr << "\nDumper-7: 'Asset' wasn't found, could not determine value for 'bIsWeakObjectPtrWithoutTag'!\n" << std::endl;
		return;
	}

	const UEStruct SoftObjectPath = ObjectArray::FindStructFast("SoftObjectPath");

	constexpr int32 SizeOfFFWeakObjectPtr = 0x08;
	constexpr int32 OldUnrealAssetPtrSize = 0x10;
	const int32 SizeOfSoftObjectPath = SoftObjectPath ? SoftObjectPath.GetStructSize() : OldUnrealAssetPtrSize;

	Settings::Internal::bIsWeakObjectPtrWithoutTag = Asset.GetSize() <= (SizeOfSoftObjectPath + SizeOfFFWeakObjectPtr);

	//std::cerr << std::format("\nDumper-7: bIsWeakObjectPtrWithoutTag = {}\n", Settings::Internal::bIsWeakObjectPtrWithoutTag) << std::endl;
}

void Settings::InitLargeWorldCoordinateSettings()
{
	const UEStruct FVectorStruct = ObjectArray::FindStructFast("Vector");

	if (!FVectorStruct) [[unlikely]]
	{
		std::cerr << "\nSomething went horribly wrong, FVector wasn't even found!\n\n" << std::endl;
		return;
	}

	const UEProperty XProperty = FVectorStruct.FindMember("X");

	if (!XProperty) [[unlikely]]
	{
		std::cerr << "\nSomething went horribly wrong, FVector::X wasn't even found!\n\n" << std::endl;
		return;
	}

		/* Check the underlaying type of FVector::X. If it's double we're on UE5.0, or higher, and using large world coordinates. */
	Settings::Internal::bUseLargeWorldCoordinates = XProperty.IsA(EClassCastFlags::DoubleProperty);

	//std::cerr << std::format("\nDumper-7: bUseLargeWorldCoordinates = {}\n", Settings::Internal::bUseLargeWorldCoordinates) << std::endl;
}

void Settings::InitObjectPtrPropertySettings()
{
	const UEClass ObjectPtrPropertyClass = ObjectArray::FindClassFast("ObjectPtrProperty");

	if (!ObjectPtrPropertyClass)
	{
		// The class doesn't exist, this so FieldPathProperty couldn't have been replaced with ObjectPtrProperty
		std::cerr << std::format("\nDumper-7: bIsObjPtrInsteadOfFieldPathProperty = {}\n", Settings::Internal::bIsObjPtrInsteadOfFieldPathProperty) << std::endl;
		Settings::Internal::bIsObjPtrInsteadOfFieldPathProperty = false;
		return;
	}

	Settings::Internal::bIsObjPtrInsteadOfFieldPathProperty = ObjectPtrPropertyClass.GetDefaultObject().IsA(EClassCastFlags::FieldPathProperty);

	std::cerr << std::format("\nDumper-7: bIsObjPtrInsteadOfFieldPathProperty = {}\n", Settings::Internal::bIsObjPtrInsteadOfFieldPathProperty) << std::endl;
}

void Settings::InitArrayDimSizeSettings()
{
	/*
	 * UEProperty::GetArrayDim() is already fully functional at this point.
	 *
	 * This setting is just there to stop it from returning (int32)0xFFFFFF01 when it should be just (uint8)0x01.
	*/
	for (const UEObject Obj : ObjectArray())
	{
		if (!Obj.IsA(EClassCastFlags::Struct))
			continue;

		const UEStruct AsStruct = Obj.Cast<UEStruct>();

		for (const UEProperty Property : AsStruct.GetProperties())
		{
			// This number should just be 0x1 to indicate it's a single element, but the upper bytes aren't cleared to zero
			if (Property.GetArrayDim() >= 0x000F0001)
			{
				Settings::Internal::bUseUint8ArrayDim = true;
				std::cerr << std::format("\nDumper-7: bUseUint8ArrayDim = {}\n", Settings::Internal::bUseUint8ArrayDim) << std::endl;
				return;
			}
		}
	}

	Settings::Internal::bUseUint8ArrayDim = false;
	std::cerr << std::format("\nDumper-7: bUseUint8ArrayDim = {}\n", Settings::Internal::bUseUint8ArrayDim) << std::endl;
}

void Settings::Config::Load()
{
	namespace fs = std::filesystem;

	// An injected DLL inherits the launcher's working directory, not necessarily
	// the game's directory. Resolve the host executable without an ANSI/MAX_PATH
	// conversion, then retain the historical working-directory/global fallbacks.
	const fs::path ExecutableDirectory = []() -> fs::path
	{
		std::wstring ModulePath(260, L'\0');
		for (;;)
		{
			const DWORD Length = GetModuleFileNameW(nullptr, ModulePath.data(), static_cast<DWORD>(ModulePath.size()));
			if (Length == 0)
				return {};
			if (Length < ModulePath.size())
			{
				ModulePath.resize(Length);
				return fs::path(ModulePath).parent_path();
			}
			if (ModulePath.size() >= 32768)
				return {};
			ModulePath.resize(ModulePath.size() < 16384 ? ModulePath.size() * 2 : 32768);
		}
	}();

	std::error_code PathError;
	const fs::path WorkingDirectory = fs::current_path(PathError);
	const fs::path ExecutableConfig = ExecutableDirectory / L"Dumper-7.ini";
	const fs::path LocalPath = WorkingDirectory / L"Dumper-7.ini";
	fs::path ConfigPath;

	if (!ExecutableDirectory.empty() && fs::exists(ExecutableConfig, PathError))
		ConfigPath = ExecutableConfig;
	else if (!WorkingDirectory.empty() && fs::exists(LocalPath, PathError))
		ConfigPath = LocalPath;
	else if (fs::exists(GlobalConfigPath, PathError))
		ConfigPath = fs::path(GlobalConfigPath);

	// If no config found, use defaults
	if (ConfigPath.empty())
	{
		return;
	}

	// Keep the existing ANSI setting-value encoding while allowing the INI file
	// itself to reside under any Unicode executable directory.
	const auto ReadConfigString = [&ConfigPath](const wchar_t* Key, const wchar_t* Default, char* Buffer, DWORD BufferSize)
	{
		std::vector<wchar_t> Value(BufferSize, L'\0');
		GetPrivateProfileStringW(L"Settings", Key, Default, Value.data(), BufferSize, ConfigPath.c_str());
		if (WideCharToMultiByte(CP_ACP, 0, Value.data(), -1, Buffer, BufferSize, nullptr, nullptr) == 0)
			Buffer[0] = '\0';
	};

	char SDKNamespace[256] = {};
	ReadConfigString(L"SDKNamespaceName", L"SDK", SDKNamespace, sizeof(SDKNamespace));

	SDKNamespaceName = SDKNamespace;

	if (strcmp(SDKNamespace, "SDK") != 0)
	{
		std::cerr << "Using custom namespace: " << SDKNamespaceName << "\n";
	}

	// Check for output path override
	char SDKPath[256] = {};
	ReadConfigString(L"SDKGenerationPath", L"C:/Dumper-7", SDKPath, sizeof(SDKPath));

	// Path issues are generally resolved during generation but its better to catch during config load
	if (strcmp(SDKPath, "C:/Dumper-7") != 0)
	{
		try
		{
			auto UserSDKPath = fs::path(SDKPath);

			// This can fail in some cases, e.g. a symbolic link to a non-existent directory can still exist
			if (!fs::exists(UserSDKPath))
			{
				// Try to create the outer path for the dump if it doesn't exist
				fs::create_directories(UserSDKPath);
			}

			std::error_code ec;
			// Try to canonicalize the input which actually opens the path to resolve any link or permission issues
			auto SDKCanonicalPath = fs::canonical(UserSDKPath, ec);
			if (!ec) 
			{
				// Report the absolute path to the user
				std::cerr << "SDK Generation Path: " << SDKCanonicalPath.string().c_str() << "\n";
				// Only actually set the SDKGenerationPath if it is valid
				Settings::Generator::SDKGenerationPath = SDKCanonicalPath.string();
			}
		}
		catch (const std::filesystem::filesystem_error& fe)
		{
			std::cerr << "Invalid SDK Generation Path: \n";
			std::cerr << fe.what() << std::endl;
			std::cerr << "Falling back to default path: C:/Dumper-7 \n";
		}
	}
	
	SleepTimeout = max(GetPrivateProfileIntW(L"Settings", L"SleepTimeout", 0, ConfigPath.c_str()), 0);

	if (SleepTimeout > 0) 
	{
		// Convert seconds to ms automatically if a value under 1 second is set
		if (SleepTimeout < 1000) 
		{
			SleepTimeout *= 1000;
		}

		std::cerr << "Sleep Timeout: " << std::dec << SleepTimeout << "ms" << "\n";
	}

	// DumpKey takes VK Scancodes as integers
	DumpKey = GetPrivateProfileIntW(L"Settings", L"DumpKey", 0, ConfigPath.c_str());

	if (DumpKey != 0) 
	{
		// Map the virtual key and print the actual name
		char KeyName[256] = {};
		LONG lParamValue = MapVirtualKeyA(DumpKey, MAPVK_VK_TO_VSC) << 16;
		if (GetKeyNameTextA(lParamValue, KeyName, sizeof(KeyName)) != 0) 
		{
			std::cerr << "Press " << KeyName << " to begin dump." << "\n";
		}
	}

	bShouldAutoUnload = GetPrivateProfileIntW(L"Settings", L"ShouldAutoUnload", bShouldAutoUnload, ConfigPath.c_str()) != 0;
	Settings::Debug::bExecuteSDKTestScript = GetPrivateProfileIntW(L"Settings", L"ExecuteSDKTestScript", Settings::Debug::bExecuteSDKTestScript, ConfigPath.c_str()) != 0;

	char LogFilePath[256] = {};
	ReadConfigString(L"LogFilePath", L"", LogFilePath, sizeof(LogFilePath));

	if (LogFilePath[0] != '\0')
	{
		std::cerr << "Log File Path: " << LogFilePath << "\n";

		// ofstream permits shared readers on Windows, so a running dump can be
		// diagnosed without closing the host or waiting for DLL unloading.
		DumpLog.open(LogFilePath, std::ios::out | std::ios::trunc);
		if (DumpLog)
		{
			OriginalCerrBuffer = std::cerr.rdbuf(DumpLog.rdbuf());
			std::cerr.clear();
		}
		else
		{
			std::cerr.clear();
			std::cerr << "Failed to open log file.\n";
		}
	}
}

void Settings::Config::DelayDumperStart()
{	
	if (DumpKey == 0)
	{
		// Sleeping for the default of 0 ms has no effect here 
		Sleep(SleepTimeout);
		return;
	}

	const auto DelayStartTime = std::chrono::high_resolution_clock::now();

	while (true)
	{
		if (GetAsyncKeyState(DumpKey) & 0x8000)
		{
			return;
		}

		if (SleepTimeout > 0) 
		{
			const auto Now = std::chrono::high_resolution_clock::now(); 
			const auto ElapsedTime = std::chrono::duration<double, std::milli>(Now - DelayStartTime); 
			if (ElapsedTime.count() > SleepTimeout) 
			{
				std::cerr << "Sleep Timeout exceeded, proceeding with dump...\n"; 
				return;
			}
		}
			
		Sleep(50);
	}
}


