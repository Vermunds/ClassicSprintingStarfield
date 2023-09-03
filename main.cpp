#include <Windows.h>
#include <Shlobj.h>

#include <vector>
#include <filesystem>
#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>

#include "version.h"

namespace Relocation
{
	void SafeWriteBuf(uintptr_t addr, void* data, size_t len)
	{
		DWORD oldProtect;

		VirtualProtect((void*)addr, len, PAGE_EXECUTE_READWRITE, &oldProtect);
		memcpy((void*)addr, data, len);
		VirtualProtect((void*)addr, len, oldProtect, &oldProtect);
	}

	
	void SafeWrite8(uintptr_t addr, uint8_t data)
	{
		SafeWriteBuf(addr, &data, sizeof(data));
	}

	void SafeWrite16(uintptr_t addr, uint16_t data)
	{
		SafeWriteBuf(addr, &data, sizeof(data));
	}

	void SafeWrite32(uintptr_t addr, uint32_t data)
	{
		SafeWriteBuf(addr, &data, sizeof(data));
	}

	void SafeWrite64(uintptr_t addr, uint64_t data)
	{
		SafeWriteBuf(addr, &data, sizeof(data));
	}

static bool SafeWriteBranch_Internal(uintptr_t src, uintptr_t dst, uint8_t op)
	{
#pragma pack(push, 1)
		struct Code
		{
			uint8_t op;
			int32_t displ;
		};
#pragma pack(pop)

		static_assert(sizeof(Code) == 5);

		ptrdiff_t delta = dst - (src + sizeof(Code));
		if ((delta < INT_MIN) || (delta > INT_MAX))
		{
			return false;
		}
		Code code;

		code.op = op;
		code.displ = delta;

		SafeWriteBuf(src, &code, sizeof(code));

		return true;
	}

	bool SafeWriteJump(uintptr_t src, uintptr_t dst)
	{
		return SafeWriteBranch_Internal(src, dst, 0xE9);
	}

	bool SafeWriteCall(uintptr_t src, uintptr_t dst)
	{
		return SafeWriteBranch_Internal(src, dst, 0xE8);
	}
}

class ButtonEvent
{
public:
	uint8_t unk[0x48];
	float value;			// 48
	float heldDownSecs;		// 4C
};
static_assert(sizeof(ButtonEvent) == 0x50); // Not actual size

class PlayerCharacter
{
public:
	
	enum class Flag10E4 : uint8_t
	{
		kSprinting = 4
	};

	uint8_t unk[0x10E4];
	Flag10E4 flag10E4;	// 10E4
	uint8_t pad10E5;
	uint8_t pad10E6;
	uint8_t pad10E7;
};
static_assert(sizeof(PlayerCharacter) == 0x10E8);  // Not actual size

PlayerCharacter** g_playerCharacter;

bool SprintHandler_ProcessButton_IsDown_Hook(ButtonEvent* a_buttonEvent)
{
	using Flag10E4 = PlayerCharacter::Flag10E4;

	if (a_buttonEvent->value > 0.0f && a_buttonEvent->heldDownSecs == 0.0f)
	{
		return true;
	}
	else if (a_buttonEvent->value == 0.0f && a_buttonEvent->heldDownSecs > 0.0f)
	{
		(*g_playerCharacter)->flag10E4 = static_cast<Flag10E4>(static_cast<uint8_t>((*g_playerCharacter)->flag10E4) & ~static_cast<uint8_t>(Flag10E4::kSprinting));
		return false;
	}
	return false;
}

bool CheckModuleVersion(const char* moduleName, uint16_t a_major, uint16_t a_minor, uint16_t a_patch, uint16_t a_beta)
{
	DWORD handle;
	DWORD versionSize = GetFileVersionInfoSizeA(moduleName, &handle);
	if (versionSize == 0)
	{
		spdlog::critical("Unable to get version info!");
		return false;
	}

	std::vector<char> versionInfoBuffer(versionSize);
	if (!GetFileVersionInfoA(moduleName, 0, versionSize, versionInfoBuffer.data()))
	{
		spdlog::critical("Unable to retrieve version info!");
		return false;
	}

	VS_FIXEDFILEINFO* fileInfo;
	UINT fileInfoSize;
	if (!VerQueryValueA(versionInfoBuffer.data(), "\\", reinterpret_cast<void**>(&fileInfo), &fileInfoSize))
	{
		spdlog::critical("Unable to query version info!");
		return false;
	}

	uint16_t moduleMajor = HIWORD(fileInfo->dwFileVersionMS);
	uint16_t moduleMinor = LOWORD(fileInfo->dwFileVersionMS);
	uint16_t moduleBuild = HIWORD(fileInfo->dwFileVersionLS);
	uint16_t moduleRevision = LOWORD(fileInfo->dwFileVersionLS);

	return (moduleMajor == a_major && moduleMinor == a_minor && moduleBuild == a_patch && moduleRevision == a_beta);
}

std::optional<std::filesystem::path> log_directory()
{
	wchar_t* buffer{ nullptr };
	const auto result = SHGetKnownFolderPath(::FOLDERID_Documents, KNOWN_FOLDER_FLAG::KF_FLAG_DEFAULT, nullptr, std::addressof(buffer));
	std::unique_ptr<wchar_t[], decltype(&CoTaskMemFree)> knownPath(buffer, CoTaskMemFree);
	if (!knownPath || result != S_OK)
	{
		return std::nullopt;
	}

	std::filesystem::path path = knownPath.get();
	path /= "My Games/Starfield/SFSE"sv;
	return path;
}

bool APIENTRY DllMain(HMODULE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	if (fdwReason == DLL_PROCESS_ATTACH)
	{
		// Create logger
		auto path = log_directory().value() / std::filesystem::path("ClassicSprintingStarfield.log"s);
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
		auto log = std::make_shared<spdlog::logger>("global log", std::move(sink));

		log->set_level(spdlog::level::trace);
		log->flush_on(spdlog::level::trace);

		spdlog::set_default_logger(std::move(log));
		spdlog::set_pattern("%g(%#): [%^%l%$] %v", spdlog::pattern_time_type::local);

		spdlog::info("Classic Sprinting Starfield v{} -({})", Version::NAME, __TIMESTAMP__);

		// Check version info
		const char* moduleName = "Starfield.exe";
		if (!CheckModuleVersion(moduleName, 1, 7, 23, 0))
		{
			spdlog::critical("Unsupported runtime version!");
			return false;
		}

		HMODULE targetModule = GetModuleHandleA(moduleName);
		if (targetModule == nullptr)
		{
			spdlog::critical("Target module not found!");
			return false;
		}

		// Install hook
		uintptr_t processButtonFuncAddr = reinterpret_cast<uintptr_t>(targetModule) + 0x1F48B30;
		uintptr_t hookAddr = processButtonFuncAddr + 0xC;
		uintptr_t codeCaveAddr = reinterpret_cast<uintptr_t>(targetModule) + 0xF9C22B; // A large-ish unused spot between some random functions
		g_playerCharacter = reinterpret_cast<PlayerCharacter**>(reinterpret_cast<uintptr_t>(targetModule) + 0x5594D28);

		Relocation::SafeWriteJump(hookAddr, (uintptr_t)codeCaveAddr);

		Relocation::SafeWrite16(codeCaveAddr, 0xB848);  // mov rax
		Relocation::SafeWrite64(codeCaveAddr + 0X2, (uintptr_t)SprintHandler_ProcessButton_IsDown_Hook);
		Relocation::SafeWrite16(codeCaveAddr + 0xA, 0xD0FF); // call rax
		Relocation::SafeWriteJump(codeCaveAddr + 0xC, (uintptr_t)hookAddr + 0x5);  // jmp
	}
	return true;
}
