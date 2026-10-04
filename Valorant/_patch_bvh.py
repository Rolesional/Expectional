# -*- coding: utf-8 -*-
path = r"c:\Users\HASAN\Discidoktorukernelkorumasi31\client-cpp\Expectional-master\Valorant\Game\catalyst_world_bvh.cpp"
with open(path, "r", encoding="utf-8") as f:
    s = f.read()
if not s.startswith("#include <stdafx.hpp>"):
    raise SystemExit("unexpected header")
start = s.find("namespace systems {")
if start < 0:
    raise SystemExit("no systems ns")
end_marker = "} // namespace systems"
end = s.rfind(end_marker)
if end < 0:
    raise SystemExit("no end marker")
body = s[start + len("namespace systems {") : end].strip("\n")
body = body.replace("math::vector3", "Vector3")
body = body.replace("g::memory.read(", "cat_mem::read(")
body = body.replace("g::memory.read<", "cat_mem::readv<")
body = body.replace(
    "g::memory.find_pattern( g::modules.client,",
    "cat_mem::find_pattern( cat_mem::client_module(),",
)
body = body.replace("g::memory.resolve_rip(", "cat_mem::resolve_rip(")
body = body.replace(
    "g::memory.find_vtable( g::modules.vphysics2,",
    "cat_mem::find_vtable( cat_mem::vphysics_module(),",
)
header = """#include \"catalyst_world_bvh.hpp\"
#include \"cat_mem_scan.hpp\"
#include \"globals.hpp\"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <unordered_set>

"""
tick = """

void TickWorldBvhParse() {
	static unsigned s_fc;
	++s_fc;
	if (!client || !cat_mem::vphysics_module())
		return;
	const bool need = !g_world_bvh.valid();
	if (!need && (s_fc % 480u) != 0u)
		return;
	g_world_bvh.parse();
}

} // namespace ex_world_bvh
"""
out = header + "namespace ex_world_bvh {\n\n" + body + tick
with open(path, "w", encoding="utf-8") as f:
    f.write(out)
print("ok chars", len(out))
