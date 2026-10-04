#pragma once

#include "structs.hpp"

namespace catalyst_grenades_port {

/** Elde bomba trajectory — Catalyst grenades.cpp (on_render 96-112) birebir, sim arka planda. */
void OnRender(const UE4Structs::view_matrix_t& vm, std::uintptr_t local_pawn,
              const UE4Structs::Vector3& eye_fallback);

}  // namespace catalyst_grenades_port
