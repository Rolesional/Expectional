#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <array>

namespace map_parser {

struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };

struct Triangle {
    Vec3 v0, v1, v2;
};

struct MapMesh {
    std::vector<Triangle> triangles;
    bool valid = false;
};

std::string get_current_map();
std::string get_debug_info();
std::string get_load_status();

MapMesh load_mesh(const std::string& map_name);

const MapMesh* get_loaded_mesh();
void clear_loaded_mesh();

struct WorkshopEntry {
    std::string map_name;
    std::string vpk_path;
};
std::vector<WorkshopEntry> list_workshop(const std::string& filter = "");

std::vector<Vec2> project_top_down(const MapMesh& mesh,
                                    float canvas_w, float canvas_h);

struct PhysFilterFlags {
    bool skip_playerclip    = true;
    bool skip_grenadeclip   = true;
    bool skip_droneclip     = true;
    bool skip_trigger       = true;
    bool skip_sky           = true;
    bool skip_toolsinvisible= true;
    bool skip_clip          = true;
};

void            set_phys_filter(const PhysFilterFlags& f);
PhysFilterFlags get_phys_filter();

}
