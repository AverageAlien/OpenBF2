// The level's AI navigation meshes for the viewport (see NavMesh.h).
#include "Framework/EditorConfig.h"
#include "App/NavMesh.h"

#include "IO/File/File.h"
#include "IO/File/FileManager.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace bf2editor
{

namespace
{

const char* const kNames[NavMesh::KindCount] = { "Infantry", "Vehicle" };
const float kCellSize = 16.0f;

bool readLoose(const std::string& path, std::string& data)
{
    std::ifstream file(path.c_str(), std::ios::binary);
    if (!file)
        return false;
    std::ostringstream text;
    text << file.rdbuf();
    data = text.str();
    return true;
}

bool readEngine(const std::string& path, std::string& data)
{
    using namespace dice::hfe;
    if (!io::fileManager)
        return false;
    io::File file;
    if (!file.open(path, io::IFileSystem::FILEMODE_READ, static_cast<io::IFileSystem::FileAccess>(0), 0))
        return false;
    const int size = file.getSize();
    if (size <= 0)
        return false;
    data.resize(static_cast<size_t>(size));
    return file.read(&data[0], static_cast<unsigned>(size)) == static_cast<unsigned>(size);
}

// A little-endian reader over the file's bytes that fails, and stays failed, past the end.
struct Reader
{
    const std::string& data;
    size_t at = 0;
    bool ok = true;
    explicit Reader(const std::string& d) : data(d) {}
    bool bytes(void* out, size_t size)
    {
        if (!ok || at + size > data.size())
            return ok = false;
        std::memcpy(out, data.data() + at, size);
        at += size;
        return true;
    }
    unsigned u32()
    {
        unsigned value = 0;
        bytes(&value, 4);
        return value;
    }
    void skip(size_t size)
    {
        if (!ok || at + size > data.size())
            ok = false;
        else
            at += size;
    }
};

} // namespace

void NavMesh::clear()
{
    for (int k = 0; k < KindCount; ++k)
        m_meshes[k] = Mesh();
    m_tried = false;
}

void NavMesh::load(const std::string& levelDirectory, const std::string& levelName, std::string& report)
{
    clear();
    m_tried = true;
    report.clear();
    for (int k = 0; k < KindCount; ++k)
    {
        const std::string file = std::string("AIPathFinding/") + kNames[k] + ".qtr";
        std::string data, from;
        if (!levelDirectory.empty() && readLoose(levelDirectory + file, data))
            from = "loose";
        else if (readEngine("Levels/" + levelName + "/" + file, data))
            from = "archive";
        std::string part;
        if (from.empty())
            part = std::string(kNames[k]) + ": no " + file;
        else
        {
            std::string error;
            if (!parse(data, m_meshes[k], error))
            {
                m_meshes[k] = Mesh();
                part = std::string(kNames[k]) + ": " + file + " unreadable (" + error + ")";
            }
            else
            {
                buildCells(m_meshes[k]);
                char text[128];
                std::snprintf(text, sizeof(text), "%s: %u polygons (%s)", kNames[k],
                              static_cast<unsigned>(m_meshes[k].polygons.size()), from.c_str());
                part = text;
            }
        }
        report += (report.empty() ? "" : "; ") + part;
    }
}

// The version-3 tree (QuadTree::readFromFile): bounds, nodes, polygons, node polygons, vertices, vertex indices,
// neighbours, after the file's island count. Only the polygons, vertices and indices are kept.
bool NavMesh::parse(const std::string& data, Mesh& mesh, std::string& error) const
{
    Reader in(data);
    const unsigned islands = in.u32();
    if (!in.ok || islands == 0)
    {
        error = "no island";
        return false;
    }
    if (islands > 1)
    {
        error = "more than one island (the AI refuses those too)";
        return false;
    }
    const unsigned version = in.u32();
    if (version != 3)
    {
        error = "version " + std::to_string(version) + ", not 3";
        return false;
    }
    in.skip(4 * 5);                                     // min (x, z), max (x, z), size
    const unsigned nodes = in.u32();
    in.skip(static_cast<size_t>(nodes) * 24);
    const unsigned polygonCount = in.u32();
    if (!in.ok || polygonCount > data.size() / 20)
    {
        error = "bad polygon count";
        return false;
    }
    struct Record { unsigned char count, material; unsigned short cluster; unsigned offset; float normal[3]; };
    static_assert(sizeof(Record) == 20, "the .qtr polygon record is 20 bytes");
    std::vector<Record> records(polygonCount);
    if (polygonCount)
        in.bytes(&records[0], records.size() * sizeof(Record));
    const unsigned nodePolygons = in.u32();
    in.skip(static_cast<size_t>(nodePolygons) * 4);
    const unsigned vertexCount = in.u32();
    if (!in.ok || vertexCount > data.size() / 12)
    {
        error = "bad vertex count";
        return false;
    }
    mesh.vertices.resize(static_cast<size_t>(vertexCount) * 3);
    if (vertexCount)
        in.bytes(&mesh.vertices[0], mesh.vertices.size() * 4);
    const unsigned indexCount = in.u32();
    if (!in.ok || indexCount > data.size() / 4)
    {
        error = "bad index count";
        return false;
    }
    mesh.indices.resize(indexCount);
    if (indexCount)
        in.bytes(&mesh.indices[0], mesh.indices.size() * 4);
    if (!in.ok)
    {
        error = "the file ends early";
        return false;
    }
    mesh.polygons.reserve(polygonCount);
    for (size_t i = 0; i < records.size(); ++i)
    {
        const Record& r = records[i];
        if (r.count < 3 || static_cast<size_t>(r.offset) + r.count > mesh.indices.size())
            continue;
        Polygon p;
        p.first = r.offset;
        p.count = r.count;
        p.material = r.material;
        std::memcpy(p.normal, r.normal, sizeof(p.normal));
        p.minX = p.minZ = 1e30f;
        p.maxX = p.maxZ = -1e30f;
        bool valid = true;
        for (unsigned c = 0; c < p.count; ++c)
        {
            const unsigned v = mesh.indices[p.first + c];
            if (v >= vertexCount)
            {
                valid = false;
                break;
            }
            const float* q = &mesh.vertices[static_cast<size_t>(v) * 3];
            p.minX = std::min(p.minX, q[0]);
            p.maxX = std::max(p.maxX, q[0]);
            p.minZ = std::min(p.minZ, q[2]);
            p.maxZ = std::max(p.maxZ, q[2]);
        }
        if (valid)
            mesh.polygons.push_back(p);
    }
    if (mesh.polygons.empty())
    {
        error = "no polygons";
        return false;
    }
    return true;
}

void NavMesh::buildCells(Mesh& mesh) const
{
    float minX = 1e30f, minZ = 1e30f, maxX = -1e30f, maxZ = -1e30f;
    for (size_t i = 0; i < mesh.polygons.size(); ++i)
    {
        minX = std::min(minX, mesh.polygons[i].minX);
        minZ = std::min(minZ, mesh.polygons[i].minZ);
        maxX = std::max(maxX, mesh.polygons[i].maxX);
        maxZ = std::max(maxZ, mesh.polygons[i].maxZ);
    }
    mesh.originX = minX;
    mesh.originZ = minZ;
    mesh.cellsX = std::max(1, static_cast<int>(std::ceil((maxX - minX) / kCellSize)) + 1);
    mesh.cellsZ = std::max(1, static_cast<int>(std::ceil((maxZ - minZ) / kCellSize)) + 1);
    mesh.cells.assign(static_cast<size_t>(mesh.cellsX) * mesh.cellsZ, std::vector<unsigned>());
    for (size_t i = 0; i < mesh.polygons.size(); ++i)
    {
        const Polygon& p = mesh.polygons[i];
        const int x0 = static_cast<int>((p.minX - minX) / kCellSize), x1 = static_cast<int>((p.maxX - minX) / kCellSize);
        const int z0 = static_cast<int>((p.minZ - minZ) / kCellSize), z1 = static_cast<int>((p.maxZ - minZ) / kCellSize);
        for (int z = z0; z <= z1 && z < mesh.cellsZ; ++z)
            for (int x = x0; x <= x1 && x < mesh.cellsX; ++x)
                mesh.cells[static_cast<size_t>(z) * mesh.cellsX + x].push_back(static_cast<unsigned>(i));
    }
}

const float* NavMesh::vertex(Kind kind, unsigned polygonCorner) const
{
    const Mesh& mesh = m_meshes[kind];
    return &mesh.vertices[static_cast<size_t>(mesh.indices[polygonCorner]) * 3];
}

void NavMesh::polygonsNear(Kind kind, float x, float z, float radius, std::vector<unsigned>& out) const
{
    out.clear();
    const Mesh& mesh = m_meshes[kind];
    if (mesh.cells.empty())
        return;
    const int x0 = std::max(0, static_cast<int>((x - radius - mesh.originX) / kCellSize));
    const int x1 = std::min(mesh.cellsX - 1, static_cast<int>((x + radius - mesh.originX) / kCellSize));
    const int z0 = std::max(0, static_cast<int>((z - radius - mesh.originZ) / kCellSize));
    const int z1 = std::min(mesh.cellsZ - 1, static_cast<int>((z + radius - mesh.originZ) / kCellSize));
    for (int cz = z0; cz <= z1; ++cz)
        for (int cx = x0; cx <= x1; ++cx)
        {
            const std::vector<unsigned>& cell = mesh.cells[static_cast<size_t>(cz) * mesh.cellsX + cx];
            for (size_t i = 0; i < cell.size(); ++i)
            {
                const Polygon& p = mesh.polygons[cell[i]];
                if (p.maxX < x - radius || p.minX > x + radius || p.maxZ < z - radius || p.minZ > z + radius)
                    continue;
                out.push_back(cell[i]);
            }
        }
    // A polygon spanning cells is listed once per cell.
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

// The AI's own edge test (QuadTree::pointWithinPolygonEdges): inside every edge, with its 0.001 slack.
bool NavMesh::inside(const Mesh& mesh, const Polygon& polygon, float x, float z) const
{
    for (unsigned c = 0; c < polygon.count; ++c)
    {
        const float* a = &mesh.vertices[static_cast<size_t>(mesh.indices[polygon.first + c]) * 3];
        const float* b = &mesh.vertices[static_cast<size_t>(mesh.indices[polygon.first + (c + 1) % polygon.count]) * 3];
        if ((b[2] - a[2]) * (x - a[0]) + (z - a[2]) * -(b[0] - a[0]) < -0.001f)
            return false;
    }
    return true;
}

bool NavMesh::heightAt(Kind kind, float x, float y, float z, float below, float above, float& height) const
{
    const Mesh& mesh = m_meshes[kind];
    if (mesh.cells.empty())
        return false;
    const int cx = static_cast<int>((x - mesh.originX) / kCellSize), cz = static_cast<int>((z - mesh.originZ) / kCellSize);
    if (x < mesh.originX || z < mesh.originZ || cx >= mesh.cellsX || cz >= mesh.cellsZ)
        return false;
    const std::vector<unsigned>& cell = mesh.cells[static_cast<size_t>(cz) * mesh.cellsX + cx];
    bool found = false;
    float best = 0.0f;
    for (size_t i = 0; i < cell.size(); ++i)
    {
        const Polygon& p = mesh.polygons[cell[i]];
        if (x < p.minX || x > p.maxX || z < p.minZ || z > p.maxZ || std::fabs(p.normal[1]) < 1e-4f
            || !inside(mesh, p, x, z))
            continue;
        const float* a = &mesh.vertices[static_cast<size_t>(mesh.indices[p.first]) * 3];
        const float h = a[1] - (p.normal[0] * (x - a[0]) + p.normal[2] * (z - a[2])) / p.normal[1];
        if (h < y - below || h > y + above)
            continue;
        if (!found || std::fabs(h - y) < std::fabs(best - y))
            best = h;
        found = true;
    }
    if (found)
        height = best;
    return found;
}

// Steps along the ray half a cell (in x/z) at a time, testing the polygons of the 3x3 cells round each step
// against their planes, until past the nearest hit.
bool NavMesh::rayHit(Kind kind, const float origin[3], const float direction[3], float reach, float out[3],
                     float& distance) const
{
    const Mesh& mesh = m_meshes[kind];
    if (mesh.cells.empty())
        return false;
    const float flat = std::sqrt(direction[0] * direction[0] + direction[2] * direction[2]);
    const float step = flat > 1e-4f ? kCellSize * 0.5f / flat : reach;
    std::vector<char> tested(mesh.polygons.size(), 0);
    bool found = false;
    float best = reach;
    for (float t = 0.0f; t <= reach && t - step <= best; t += step)
    {
        const int cx = static_cast<int>(std::floor((origin[0] + direction[0] * t - mesh.originX) / kCellSize));
        const int cz = static_cast<int>(std::floor((origin[2] + direction[2] * t - mesh.originZ) / kCellSize));
        for (int z = std::max(0, cz - 1); z <= std::min(mesh.cellsZ - 1, cz + 1); ++z)
            for (int x = std::max(0, cx - 1); x <= std::min(mesh.cellsX - 1, cx + 1); ++x)
            {
                const std::vector<unsigned>& cell = mesh.cells[static_cast<size_t>(z) * mesh.cellsX + x];
                for (size_t i = 0; i < cell.size(); ++i)
                {
                    if (tested[cell[i]])
                        continue;
                    tested[cell[i]] = 1;
                    const Polygon& p = mesh.polygons[cell[i]];
                    const float facing = p.normal[0] * direction[0] + p.normal[1] * direction[1] + p.normal[2] * direction[2];
                    if (std::fabs(facing) < 1e-6f)
                        continue;
                    const float* a = &mesh.vertices[static_cast<size_t>(mesh.indices[p.first]) * 3];
                    const float along = (p.normal[0] * (a[0] - origin[0]) + p.normal[1] * (a[1] - origin[1])
                                         + p.normal[2] * (a[2] - origin[2])) / facing;
                    if (along < 0.0f || along >= best)
                        continue;
                    const float hx = origin[0] + direction[0] * along, hz = origin[2] + direction[2] * along;
                    if (hx < p.minX || hx > p.maxX || hz < p.minZ || hz > p.maxZ || !inside(mesh, p, hx, hz))
                        continue;
                    best = along;
                    found = true;
                }
            }
    }
    if (!found)
        return false;
    distance = best;
    for (int i = 0; i < 3; ++i)
        out[i] = origin[i] + direction[i] * best;
    return true;
}

} // namespace bf2editor
