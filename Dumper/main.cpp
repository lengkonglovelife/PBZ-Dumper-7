#include <Windows.h>
#include <iostream>
#include <chrono>
#include <fstream>

#include "Generators/CppGenerator.h"
#include "Generators/MappingGenerator.h"
#include "Generators/IDAMappingGenerator.h"
#include "Generators/DumpspaceGenerator.h"

#include "Generators/Generator.h"

enum class EFortToastType : uint8
{
    Default                                  = 0,
    Subdued                                  = 1,
    Impactful                                = 2,
    Lock                                     = 3,
    EFortToastType_MAX                       = 4,
};

static void GenerateDump()
{
	std::cerr << "Started Generation [Dumper-7]!\n";
	auto DumpStartTime = std::chrono::high_resolution_clock::now();

	Generator::InitEngineCore();
	Generator::InitInternal();

	FetchGameNameAndVersion();

	std::cerr << "GameName: " << Settings::Generator::GameName << "\n";
	std::cerr << "GameVersion: " << Settings::Generator::GameVersion << "\n\n";

	std::cerr << "FolderName: " << (Settings::Generator::GameVersion + '-' + Settings::Generator::GameName) << "\n\n";

	Generator::Generate<CppGenerator>();
	Generator::Generate<MappingGenerator>();
	Generator::Generate<IDAMappingGenerator>();
	Generator::Generate<DumpspaceGenerator>();

	auto DumpFinishTime = std::chrono::high_resolution_clock::now();

	std::chrono::duration<double, std::milli> DumpTime = DumpFinishTime - DumpStartTime;

	std::cerr << "\n\nGenerating SDK took (" << DumpTime.count() << "ms)\n\n\n";

	if (Settings::Debug::bExecuteSDKTestScript)
	{
		/* Executes a python script to test if the SDK compiles correctly. */
		CppGenerator::ExecuteSDKCompilationTestScript();
	}
	std::cerr << "Generation complete [Dumper-7]\n";
}

static DWORD GenerateWithCppErrors()
{
	try { GenerateDump(); return ERROR_SUCCESS; }
	catch (const std::exception& Error)
	{
		std::cerr << "Generation failed [Dumper-7]: " << Error.what() << '\n';
		return ERROR_INVALID_DATA;
	}
}

static int ReportDumpException(EXCEPTION_POINTERS* Info)
{
	const auto* Record = Info->ExceptionRecord;
	if (Record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION && Record->ExceptionCode != EXCEPTION_IN_PAGE_ERROR)
		return EXCEPTION_CONTINUE_SEARCH;
	std::cerr << "Generation failed [Dumper-7]: exception=0x" << std::hex << Record->ExceptionCode
		<< " address=" << Record->ExceptionAddress;
	if (Record->NumberParameters >= 2)
		std::cerr << " operation=" << Record->ExceptionInformation[0] << " target=0x" << Record->ExceptionInformation[1];
	std::cerr << '\n';
	void* Frames[24]{};
	const USHORT Count = CaptureStackBackTrace(0, 24, Frames, nullptr);
	for (USHORT Index = 0; Index < Count; ++Index)
	{
		HMODULE Owner = nullptr;
		char Name[MAX_PATH]{};
		if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			reinterpret_cast<LPCSTR>(Frames[Index]), &Owner))
			GetModuleFileNameA(Owner, Name, MAX_PATH);
		std::cerr << "frame=" << Frames[Index] << " module=" << Name << " rva=0x"
			<< (reinterpret_cast<uintptr_t>(Frames[Index]) - reinterpret_cast<uintptr_t>(Owner)) << '\n';
	}
	return EXCEPTION_EXECUTE_HANDLER;
}

static DWORD GenerateGuarded()
{
	__try { return GenerateWithCppErrors(); }
	__except (ReportDumpException(GetExceptionInformation())) { return ERROR_INVALID_ADDRESS; }
}

DWORD MainThread(HMODULE Module)
{
	std::cerr.clear();
	std::cerr << std::boolalpha << std::hex;
	Settings::Config::Load();
	std::cerr << "Initializing [Dumper-7] PBZ update 20261104\n";
	Settings::Config::DelayDumperStart();
	const DWORD Result = GenerateGuarded();

	std::cerr << "\n\nPress F6 to unload\n\n\n";

	while (true)
	{
		if (Result != ERROR_SUCCESS || GetAsyncKeyState(VK_F6) & 1 || Settings::Config::bShouldAutoUnload)
		{
			std::cerr << "\nUnloaded [Dumper-7]\n";

			Settings::Config::CloseLog();
			FreeLibraryAndExitThread(Module, Result);
		}

		Sleep(100);
	}

	return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
	switch (reason)
	{
	case DLL_PROCESS_ATTACH:
		if (HANDLE Thread = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)MainThread, hModule, 0, 0))
			CloseHandle(Thread);
		break;
	}
	
	return TRUE;
}
