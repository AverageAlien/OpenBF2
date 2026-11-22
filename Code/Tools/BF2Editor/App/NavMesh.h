// The level's AI navigation meshes (AIPathFinding/Infantry.qtr and Vehicle.qtr), read for the viewport's
// navmesh overlay and its on-mesh checks of spawn points and spawners. [CHOSEN, not recovered]: the retail
// editor drew them through the AI's debug renderer, whose rebuilt bodies are still frontiers, so the editor
// reads the files itself (the version-3 layout QuadTree::readFromFile reads). Editor-only code.
#ifndef BF2EDITOR_APP_NAVMESH_H
#define BF2EDITOR_APP_NAVMESH_H

#include "Framework/EditorConfig.h"

#include <string>
#include <vector>

namespace bf2editor
{

class NavMesh
{
public:
    enum Kind { Infantry = 0, Vehicle = 1, KindCount = 2 };

    struct Polygon
    {
        unsigned first;         // into indices()
        unsigned count;
        unsigned char material;
        float normal[3];
        float minX, minZ, maxX, maxZ;
    };

    // Reads both meshes: the level folder's loose files first, else the engine's (the level's mounted
    // archives under Levels/<level>/). `report` says what was found.
    void load(const std::string& levelDirectory, const std::string& levelName, std::string& report);
    void clear();
    bool loaded(Kind kind) const { return !m_meshes[kind].polygons.empty(); }
    bool tried() const { return m_tried; }

    const std::vector<Polygon>& polygons(Kind kind) const { return m_meshes[kind].polygons; }
    const float* vertex(Kind kind, unsigned polygonCorner) const;
    // The polygons whose bounds meet the square of `radius` round (x, z).
    void polygonsNear(Kind kind, float x, float z, float radius, std::vector<unsigned>& out) const;
    // The mesh's height under (x, z) nearest to y, among polygons from `below` under y to `above` over it.
    bool heightAt(Kind kind, float x, float y, float z, float below, float above, float& height) const;
    // The nearest point within `reach` where the ray (unit `direction`) meets the mesh, and its distance.
    bool rayHit(Kind kind, const float origin[3], const float direction[3], float reach, float out[3],
                float& distance) const;

private:
    struct Mesh
    {
        std::vector<float>    vertices;     // x, y, z
        std::vector<unsigned> indices;
        std::vector<Polygon>  polygons;
        float originX = 0.0f, originZ = 0.0f;
        int   cellsX = 0, cellsZ = 0;
        std::vector<std::vector<unsigned> > cells;
    };
    bool parse(const std::string& data, Mesh& mesh, std::string& error) const;
    void buildCells(Mesh& mesh) const;
    bool inside(const Mesh& mesh, const Polygon& polygon, float x, float z) const;

    Mesh m_meshes[KindCount];
    bool m_tried = false;
};

} // namespace bf2editor

#endif
