#include <shlobj.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>
#include <vector>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include "sfse/Hooks_Version.h"
#include "sfse/PluginAPI.h"
#include "sfse/PluginManager.h"
#include "sfse_common/Relocation.h"
#include "sfse_common/BranchTrampoline.h"
#include "sfse_common/SafeWrite.h"
#include "sfse_common/sfse_version.h"

#include "version.h"

const RelocAddr<uintptr_t*> processButtonFuncAddr = 0x1F48B00;
const RelocAddr<uintptr_t*> playerCharacterSingletonAddr = 0x5595BA8;

class ButtonEvent
{
public:
	uint8_t unk[0x48];
	float value;         // 48
	float heldDownSecs;  // 4C
};
static_assert(sizeof(ButtonEvent) == 0x50);  // Not actual size

class PlayerCharacter
{
public:
	enum class Flag10E4 : uint8_t
	{
		kSprinting = 4
	};

	uint8_t unk[0x10E4];
	Flag10E4 flag10E4;  // 10E4
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

std::optional<std::filesystem::path> LogDirectory()
{
	wchar_t* buffer{ nullptr };
	const auto result = SHGetKnownFolderPath(::FOLDERID_Documents, KNOWN_FOLDER_FLAG::KF_FLAG_DEFAULT, nullptr, std::addressof(buffer));
	std::unique_ptr<wchar_t[], decltype(&CoTaskMemFree)> knownPath(buffer, CoTaskMemFree);
	if (!knownPath || result != S_OK)
	{
		return std::nullopt;
	}

	std::filesystem::path path = knownPath.get();
	path /= "My Games/Starfield/SFSE/Logs"sv;
	return path;
}

constexpr std::array<char, 256> CreateStringArray(const char* a_str) noexcept
{
	std::array<char, 256> result{};
	for (size_t i = 0; i < result.size() && a_str[i] != '\0'; ++i)
	{
		result[i] = a_str[i];
	}
	return result;
}

DLLEXPORT constinit auto SFSEPlugin_Version = []() noexcept {
	SFSEPluginVersionData data{};

	data.dataVersion = SFSEPluginVersionData::kVersion;
	data.pluginVersion = Version::MAJOR;

	constexpr auto name = CreateStringArray(Version::PROJECT.data());
	std::copy(name.begin(), name.end(), data.name);

	constexpr auto author = CreateStringArray("Vermunds");
	std::copy(author.begin(), author.end(), data.author);

	data.addressIndependence = 0;
	data.structureIndependence = SFSEPluginVersionData::kStructureIndependence_InitialLayout;

	data.compatibleVersions[0] = RUNTIME_VERSION_1_7_29;
	data.compatibleVersions[1] = 0;

	data.seVersionRequired = 0;
	data.reservedNonBreaking = 0;
	data.reservedBreaking = 0;

	return data;
}();

DLLEXPORT bool SFSEAPI SFSEPlugin_Load(SFSEInterface* a_sfse)
{
#ifndef NDEBUG
	constexpr int waitForDebuggerTimeoutSecs = 5;

	auto startTime = std::chrono::steady_clock::now();
	while (!IsDebuggerPresent())
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

		auto currentTime = std::chrono::steady_clock::now();
		if (std::chrono::duration_cast<std::chrono::seconds>(currentTime - startTime).count() >= waitForDebuggerTimeoutSecs)
		{
			break;
		}
	}
#endif

	// Check for broken install
	std::filesystem::path asiFilePath = std::filesystem::current_path() / "plugins" / "ClassicSprintingStarfield.asi";
	if (std::filesystem::exists(asiFilePath))
	{
		const char* errorMessage =
			"An old version of Classic Sprinting for Starfield is installed.\n\n"
			"Please delete \"ClassicSprintingStarfield.asi\" in the plugins folder, inside the root directory of the game.\n\n"
			"Until this file is not deleted, the new version will be disabled.";
		MessageBoxA(NULL, errorMessage, "Classic Sprinting for Starfield", MB_OK | MB_ICONERROR);

		return false;
	}

	if (!g_branchTrampoline.create(1 << 4))
	{
		spdlog::critical("Couldn't create branch trampoline. The mod is disabled.");
		return false;
	}

	// Create logger
	auto path = LogDirectory().value() / std::filesystem::path("ClassicSprintingStarfield.log"s);
	auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
	auto log = std::make_shared<spdlog::logger>("global log", std::move(sink));

	log->set_level(spdlog::level::trace);
	log->flush_on(spdlog::level::trace);

	spdlog::set_default_logger(std::move(log));
	spdlog::set_pattern("%g(%#): [%^%l%$] %v", spdlog::pattern_time_type::local);

	spdlog::info("Classic Sprinting Starfield v{} -({})", Version::NAME, __TIMESTAMP__);

	// Install hook
	uintptr_t hookAddr = processButtonFuncAddr.getUIntPtr() + 0xC;
	g_playerCharacter = reinterpret_cast<PlayerCharacter**>(playerCharacterSingletonAddr.getUIntPtr());
	g_branchTrampoline.write5Call(hookAddr, (uintptr_t)SprintHandler_ProcessButton_IsDown_Hook);

	return true;
}
