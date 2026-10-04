#pragma once
#include "../weapon_runtime.hpp"
#include "../structs.hpp"

#include <cstdint>
#include <vector>

namespace TriggerBot {
	void Run(int localTeam, const LegitCombatSettings& cfg, const std::vector<UE4Structs::CS2Entity>& players);
	
	bool ToggleArmForUi();
}
