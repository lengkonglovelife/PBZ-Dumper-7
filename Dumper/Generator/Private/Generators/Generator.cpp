
#include "Generators/Generator.h"
#include "Managers/StructManager.h"
#include "Managers/EnumManager.h"
#include "Managers/MemberManager.h"
#include "Managers/PackageManager.h"

#include "HashStringTable.h"
#include "Utils.h"

#include "Platform.h"
#include "Json/json.hpp"

#include <fstream>
#include <stdexcept>

inline void InitSettings()
{
	Settings::InitWeakObjectPtrSettings();
	Settings::InitLargeWorldCoordinateSettings();

	Settings::InitObjectPtrPropertySettings();
	Settings::InitArrayDimSizeSettings();
}


void Generator::InitEngineCore()
{
	/* manual override */
	//ObjectArray::Init(/*GObjects*/, /*Layout = Default*/); // FFixedUObjectArray (UEVersion < UE4.21)
	//ObjectArray::Init(/*GObjects*/, /*ChunkSize*/, /*Layout = Default*/); // FChunkedFixedUObjectArray (UEVersion >= UE4.21)

	//FName::Init(/*bForceGNames = false*/);
	//FName::Init(/*AppendString, FName::EOffsetOverrideType::AppendString*/);
	//FName::Init(/*ToString, FName::EOffsetOverrideType::ToString*/);
	//FName::Init(/*GNames, FName::EOffsetOverrideType::GNames, true/false*/);
 
	//Off::InSDK::ProcessEvent::InitPE(/*PEIndex*/);

	/* Back4Blood (requires manual GNames override) */
	//InitObjectArrayDecryption([](void* ObjPtr) -> uint8* { return reinterpret_cast<uint8*>(uint64(ObjPtr) ^ 0x8375); });

	/* Multiversus [Unsupported, weird GObjects-struct] */
	//InitObjectArrayDecryption([](void* ObjPtr) -> uint8* { return reinterpret_cast<uint8*>(uint64(ObjPtr) ^ 0x1B5DEAFD6B4068C); });

	ObjectArray::Init();

	CALL_PLATFORM_SPECIFIC_FUNCTION(FName::Init);

	Off::Init();
	PropertySizes::Init();

	CALL_PLATFORM_SPECIFIC_FUNCTION(Off::InSDK::ProcessEvent::InitPE); // Must be at this position, relies on offsets initialized in Off::Init()

	Off::InSDK::Find::InitFindFunctionChecked();

	// Reflection registration completes before the startup map and its asset
	// packages are loaded. Building managers during that transition leaves
	// newly arriving types absent from their maps during generation.
	const ULONGLONG WorldDeadline = GetTickCount64() + 120000;
	ULONGLONG StableSince = GetTickCount64();
	int32 PreviousCount = ObjectArray::Num();
	for (;;)
	{
		const UEClass WorldClass = ObjectArray::FindClassFast("World");
		bool HasWorld = false;
		if (WorldClass)
		{
			for (UEObject Obj : ObjectArray())
			{
				if (!Obj.HasAnyFlags(EObjectFlags::ClassDefaultObject) && Obj.IsA(WorldClass))
				{
					HasWorld = true;
					break;
				}
			}
		}
		const int32 Count = ObjectArray::Num();
		if (!HasWorld || Count != PreviousCount)
			StableSince = GetTickCount64();
		PreviousCount = Count;
		if (HasWorld && GetTickCount64() - StableSince >= 5000)
			break;
		if (GetTickCount64() >= WorldDeadline)
			throw std::runtime_error("World/startup packages did not settle; dump stopped without exiting host");
		std::cerr << "Waiting for loaded world/packages, objects=" << std::dec << Count << std::hex << '\n';
		Sleep(500);
	}

	Off::InSDK::World::InitGWorld(); // Must be at this position, relies on offsets initialized in Off::Init()

	Off::InSDK::Text::InitTextOffsets(); // Must be at this position, relies on offsets initialized in Off::InitPE()

	InitSettings();
}

void Generator::InitInternal()
{
	// Initialize PackageManager with all packages, their names, structs, classes enums, functions and dependencies
	std::cerr << "InitInternal: PackageManager::Init begin\n";
	PackageManager::Init();
	std::cerr << "InitInternal: PackageManager::Init done\n";

	// Initialize StructManager with all structs and their names
	std::cerr << "InitInternal: StructManager::Init begin\n";
	StructManager::Init();
	std::cerr << "InitInternal: StructManager::Init done\n";
	
	// Initialize EnumManager with all enums and their names
	std::cerr << "InitInternal: EnumManager::Init begin\n";
	EnumManager::Init();
	std::cerr << "InitInternal: EnumManager::Init done\n";
	
	// Initialized all Member-Name collisions
	std::cerr << "InitInternal: MemberManager::Init begin\n";
	MemberManager::Init();
	std::cerr << "InitInternal: MemberManager::Init done\n";

	// Post-Initialize PackageManager after StructManager has been initialized. 'PostInit()' handles Cyclic-Dependencies detection
	std::cerr << "InitInternal: PackageManager::PostInit begin\n";
	PackageManager::PostInit();
	std::cerr << "InitInternal: PackageManager::PostInit done\n";
}

void FetchGameNameAndVersion()
{
	/* Kismet only; a one-sided Settings.h override no longer blocks the other field. */
	if (!Settings::Generator::GameName.empty() && !Settings::Generator::GameVersion.empty())
		return;

	UEClass Kismet = ObjectArray::FindClassFast("KismetSystemLibrary");

	if (Settings::Generator::GameVersion.empty() && Kismet)
	{
		if (UEFunction GetEngineVersion = Kismet.GetFunction("KismetSystemLibrary", "GetEngineVersion"))
		{
			FString Version;
			Kismet.ProcessEvent(GetEngineVersion, &Version);
			Settings::Generator::GameVersion = Version.ToString();
		}
	}

	if (Settings::Generator::GameName.empty() && Kismet)
	{
		if (UEFunction GetGameName = Kismet.GetFunction("KismetSystemLibrary", "GetGameName"))
		{
			FString Name;
			Kismet.ProcessEvent(GetGameName, &Name);
			Settings::Generator::GameName = Name.ToString();
		}
	}
}

bool Generator::SetupDumperFolder()
{
	try
	{
		std::string FolderName = (Settings::Generator::GameVersion + '-' + Settings::Generator::GameName);
		FileNameHelper::MakeValidFileName(FolderName);

		DumperFolder = fs::path(Settings::Generator::SDKGenerationPath) / FolderName;
		if (fs::exists(DumperFolder))
		{
			fs::path OldFolder = DumperFolder;

			if (Settings::Generator::bCreateUniqueBackups)
			{
				std::time_t Now = std::time(nullptr);
				OldFolder += ("_" + std::to_string(Now));
			}
			else
			{
				OldFolder += "_OLD";
			}

			std::cerr << "Folder already exists. Backing up to: " << OldFolder.generic_string() << "\n";

			fs::remove_all(OldFolder);
			fs::rename(DumperFolder, OldFolder);
		}

		fs::create_directories(DumperFolder);
	}
	catch (const std::filesystem::filesystem_error& fe)
	{
		std::cerr << "Could not create required folders! Info: \n";
		std::cerr << fe.what() << std::endl;
		return false;
	}

	return true;
}

bool Generator::SetupFolders(std::string& FolderName, fs::path& OutFolder)
{
	fs::path Dummy;
	std::string EmptyName = "";
	return SetupFolders(FolderName, OutFolder, EmptyName, Dummy);
}

bool Generator::SetupFolders(std::string& FolderName, fs::path& OutFolder, std::string& SubfolderName, fs::path& OutSubFolder)
{
	FileNameHelper::MakeValidFileName(FolderName);
	FileNameHelper::MakeValidFileName(SubfolderName);

	try
	{
		OutFolder = DumperFolder / FolderName;
		OutSubFolder = OutFolder / SubfolderName;
				
		if (fs::exists(OutFolder))
		{
			fs::path Old = OutFolder.generic_string() + "_OLD";

			fs::remove_all(Old);

			fs::rename(OutFolder, Old);
		}

		fs::create_directories(OutFolder);

		if (!SubfolderName.empty())
			fs::create_directories(OutSubFolder);
	}
	catch (const std::filesystem::filesystem_error& fe)
	{
		std::cerr << "Could not create required folders! Info: \n";
		std::cerr << fe.what() << std::endl;
		return false;
	}

	return true;
}


void DumpEditorOnlyMetadata(const fs::path& DumperFolder)
{
	if (Off::FField::EditorOnlyMetadata == -1)
		return;

	nlohmann::json MetadataJson;
	MetadataJson["GameName"] = Settings::Generator::GameName;
	MetadataJson["GameVersion"] = Settings::Generator::GameVersion;

	for (UEObject Obj : ObjectArray())
	{
		if (!Obj.IsA(EClassCastFlags::Struct))
			continue;

		UEStruct Struct = Obj.Cast<UEStruct>();

		std::vector<UEProperty> ChildProperties = Struct.GetProperties();
		if (ChildProperties.empty()) // Avoids allocating string for GetCppName() and prevents json from auto-creating empty objects for property-less structs
			continue;

		auto& StructMembers = MetadataJson[Struct.GetCppName()];
		for (UEProperty Prop : ChildProperties)
		{
			auto& Entries = StructMembers[Prop.GetValidName()];

			for (const auto& [Key, Value] : Prop.Cast<UEFField>().GetMetaData())
			{
				if (Key.empty() && Value.empty())
					continue;

				Entries[Key] = Value;
			}
		}
	}

	std::ofstream MetadataFile(DumperFolder / "Metadata.json");
	MetadataFile << MetadataJson.dump(4);
}
