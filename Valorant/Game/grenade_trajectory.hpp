#pragma once

#include <cstdint>

namespace UE4Structs {
struct view_matrix_t;
struct Vector3;
}  // namespace UE4Structs

namespace expectional_trajectory {

/** Catalyst CView + setup_throw worker'da; render IOCTL/sim yok (lag onleme). */
void DrawFrame(const UE4Structs::view_matrix_t& vm, std::uintptr_t local_pawn,
               const UE4Structs::Vector3& eye_world);

}  // namespace expectional_trajectory
