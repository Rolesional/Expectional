#pragma once
#include "../weapon_runtime.hpp"
#include "../structs.hpp"

#include <cstdint>
#include <vector>

/** ananbaban `TriggerBot` benzeri: crosshair entity + Catalyst isin taramasi + sol tik. */
namespace TriggerBot {
	void Run(int localTeam, const LegitCombatSettings& cfg, const std::vector<UE4Structs::CS2Entity>& players);
	/** Toggle modu aktif mi (Misc keybind listesi). */
	bool ToggleArmForUi();
}
