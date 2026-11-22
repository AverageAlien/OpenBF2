// The AI strategic areas' panel and viewport tools (see StrategicAreaTools.h).
#include "Framework/EditorConfig.h"
#include "App/StrategicAreaTools.h"

#include "App/EditSession.h"
#include "App/FreeCamera.h"
#include "App/ImGuiHost.h"
#include "App/StrategicAreas.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace bf2editor
{

namespace StrategicAreaTools
{

namespace
{

const char* const kMaps[2] = { "Infantry", "Vehicle" };

// The press being dragged: an order position (kind 1, `index` its map) or a waypoint (kind 2, `index` its number).
struct Drag
{
    int         kind = 0;
    std::string area;
    int         index = -1;
    bool        moved = false;
    float       target[3] = { 0.0f, 0.0f, 0.0f };
    int         before = -1;
};

Drag g_drag;
bool g_wayPointTool = false;       // clicks add waypoints from the selected area to g_wayPointTo
int  g_wayPointMap = 0;
std::string g_wayPointTo;
bool g_orderTool = false;          // the next click sets the selected area's order position g_orderMap
int  g_orderMap = 0;
char g_newName[64] = "";
float g_newRadius = 50.0f;

bool project(const float viewProjection[16], const float p[3], ImVec2& out)
{
    const ImGuiIO& io = ImGui::GetIO();
    const float* m = viewProjection;
    const float x = p[0] * m[0] + p[1] * m[4] + p[2] * m[8] + m[12];
    const float y = p[0] * m[1] + p[1] * m[5] + p[2] * m[9] + m[13];
    const float w = p[0] * m[3] + p[1] * m[7] + p[2] * m[11] + m[15];
    if (w < 0.05f)
        return false;
    out.x = (x / w * 0.5f + 0.5f) * io.DisplaySize.x;
    out.y = (0.5f - y / w * 0.5f) * io.DisplaySize.y;
    return true;
}

// Where an area stands: its control point's position (by the id it was made from), or a free area's centre at its
// height. False when its control point is not in the open layer.
bool centreOf(const StrategicArea& area, const std::vector<EditSession::LevelObjectRow>& points, EditSession& session,
              float out[3])
{
    if (!area.created)
        return false;
    if (area.fromControlPoint)
    {
        for (size_t i = 0; i < points.size(); ++i)
            if (points[i].id == area.controlPointId)
            {
                out[0] = points[i].position[0];
                out[1] = points[i].position[1];
                out[2] = points[i].position[2];
                return true;
            }
        return false;
    }
    out[0] = (area.corner[0][0] + area.corner[1][0]) * 0.5f;
    out[2] = (area.corner[0][1] + area.corner[1][1]) * 0.5f;
    out[1] = area.height;
    float ground = 0.0f;
    if (session.groundHeight(out[0], out[2], ground) && (area.height <= -600.0f || area.height < ground))
        out[1] = ground;
    return true;
}

void groundCircle(ImDrawList* list, EditSession& session, const float viewProjection[16], const float centre[3],
                  float radius, ImU32 colour, float thickness)
{
    ImVec2 previous;
    bool previousVisible = false, firstVisible = false;
    ImVec2 first;
    const int steps = 48;
    for (int s = 0; s <= steps; ++s)
    {
        const float angle = 6.2831853f * static_cast<float>(s) / steps;
        float p[3] = { centre[0] + std::cos(angle) * radius, centre[1], centre[2] + std::sin(angle) * radius };
        float ground = 0.0f;
        if (session.groundHeight(p[0], p[2], ground))
            p[1] = ground + 1.0f;
        ImVec2 at;
        const bool visible = project(viewProjection, p, at);
        if (s == 0)
        {
            first = at;
            firstVisible = visible;
        }
        if (visible && previousVisible)
            list->AddLine(previous, at, colour, thickness);
        previous = at;
        previousVisible = visible;
    }
    (void)first;
    (void)firstVisible;
}

void arrow(ImDrawList* list, const ImVec2& a, const ImVec2& b, ImU32 colour, float thickness, bool head)
{
    list->AddLine(a, b, colour, thickness);
    if (!head)
        return;
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length < 20.0f)
        return;
    const float ux = dx / length, uy = dy / length;
    const ImVec2 tip(a.x + dx * 0.6f, a.y + dy * 0.6f);
    list->AddTriangleFilled(tip, ImVec2(tip.x - ux * 12.0f - uy * 6.0f, tip.y - uy * 12.0f + ux * 6.0f),
                            ImVec2(tip.x - ux * 12.0f + uy * 6.0f, tip.y - uy * 12.0f - ux * 6.0f), colour);
}

bool hasNeighbour(const StrategicArea& area, const std::string& name)
{
    for (size_t i = 0; i < area.neighbours.size(); ++i)
        if (_stricmp(area.neighbours[i].c_str(), name.c_str()) == 0)
            return true;
    return false;
}

void removeNeighbour(StrategicArea& area, const std::string& name)
{
    for (size_t i = area.neighbours.size(); i-- > 0;)
        if (_stricmp(area.neighbours[i].c_str(), name.c_str()) == 0)
            area.neighbours.erase(area.neighbours.begin() + static_cast<std::ptrdiff_t>(i));
}

// The surface point under the mouse, a little raised (an order position stands on the ground).
int mapOf(const StrategicWayPoint& point)
{
    return _stricmp(point.map.c_str(), kMaps[1]) == 0 ? 1 : 0;
}

// Where the mouse puts a point bots go to on `map`: that map's navmesh where the view meets it, unless the surface
// the view shows is clearly nearer (the mesh floats a little over the ground); else that surface. Objects of
// locked layers and unpickable classes count, so points go onto balconies and roofs as the AI walks them.
bool mouseTarget(EditSession& session, int map, float out[3])
{
    const ImGuiIO& io = ImGui::GetIO();
    float origin[3], direction[3];
    session.camera().rayThroughPixel(io.MousePos.x, io.MousePos.y, session.viewWidth(), session.viewHeight(), origin,
                                     direction);
    std::string detail;
    float surface[3];
    const bool onSurface = session.surfaceAt(io.MousePos.x, io.MousePos.y, surface, detail, false, true);
    float surfaceDistance = 1e30f;
    if (onSurface)
    {
        const float dx = surface[0] - origin[0], dy = surface[1] - origin[1], dz = surface[2] - origin[2];
        surfaceDistance = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    const NavMesh& mesh = session.navMesh();
    const NavMesh::Kind kind = map == 1 ? NavMesh::Vehicle : NavMesh::Infantry;
    float distance = 0.0f;
    if (mesh.rayHit(kind, origin, direction, session.camera().farPlane(), out, distance)
        && distance <= surfaceDistance + 3.0f)
        return true;
    if (!onSurface)
        return false;
    std::copy(surface, surface + 3, out);
    return true;
}

// Whether `p` lies on the map's navmesh (within 2 m up or down): bots can only be sent to points on it.
const char* meshStatus(EditSession& session, int map, const float p[3], bool& onMesh)
{
    const NavMesh& mesh = session.navMesh();
    const NavMesh::Kind kind = map == 1 ? NavMesh::Vehicle : NavMesh::Infantry;
    onMesh = true;
    if (!mesh.loaded(kind))
        return "no navmesh";
    float height = 0.0f;
    onMesh = mesh.heightAt(kind, p[0], p[1], p[2], 2.0f, 2.0f, height);
    return onMesh ? "on navmesh" : "OFF the navmesh";
}

// x / y / z typed in (Enter applies); true when changed.
bool positionField(const char* id, float p[3])
{
    float edit[3] = { p[0], p[1], p[2] };
    ImGui::SetNextItemWidth(260.0f);
    if (!ImGui::InputFloat3(id, edit, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue))
        return false;
    std::copy(edit, edit + 3, p);
    return true;
}

} // namespace

void drawPanel(EditSession& session, bool* open)
{
    if (!ImGui::Begin("AI strategic areas", open))
    {
        ImGui::End();
        return;
    }
    StrategicAreas& ai = session.strategicAreas();
    if (!ai.loaded())
    {
        ImGui::TextDisabled("No level open, or its StrategicAreas.ai could not be read (see the log).");
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("%s", ai.path().c_str());
    ImGui::TextDisabled("Saved with the level (File > Save). The game reads server.zip: pack the level after saving.");
    if (!session.overlays().strategicAreas && ImGui::SmallButton("Show them in the view"))
        session.runCommand("EditorView.overlay strategicAreas 1", "overlay");
    std::vector<EditSession::LevelObjectRow> points = session.controlPoints();

    // The areas.
    std::vector<StrategicArea>& areas = ai.areas();
    if (ImGui::BeginListBox("##areas", ImVec2(-1.0f, 7.0f * ImGui::GetTextLineHeightWithSpacing())))
    {
        for (size_t i = 0; i < areas.size(); ++i)
        {
            std::string label = areas[i].name;
            if (!areas[i].created)
                label += "  (set up only)";
            else if (!areas[i].fromControlPoint)
                label += "  (free area)";
            if (ImGui::Selectable(label.c_str(), _stricmp(ai.selectedArea.c_str(), areas[i].name.c_str()) == 0))
            {
                ai.selectedArea = areas[i].name;
                ai.selectedOrder = ai.selectedWayPoint = -1;
            }
        }
        ImGui::EndListBox();
    }
    // New areas: from a control point (one per point) or free, at the view's centre.
    if (ImGui::BeginCombo("##fromPoint", "New area from control point..."))
    {
        for (size_t i = 0; i < points.size(); ++i)
        {
            bool taken = false;
            for (size_t k = 0; k < areas.size(); ++k)
                taken = taken || (areas[k].fromControlPoint && areas[k].controlPointId == points[i].id);
            char label[160];
            std::snprintf(label, sizeof(label), "%s (id %d)%s", points[i].name.c_str(), points[i].id, taken ? "  - has one" : "");
            if (ImGui::Selectable(label, false, taken ? ImGuiSelectableFlags_Disabled : 0))
            {
                const int before = ai.current();
                StrategicArea area;
                area.name = points[i].name;
                area.controlPointId = points[i].id;
                area.radius = 50.0f;
                area.flags.push_back("ControlPoint");
                area.hasOrder[0] = area.hasOrder[1] = true;
                for (int m = 0; m < 2; ++m)
                    for (int k = 0; k < 3; ++k)
                        area.order[m][k] = points[i].position[k];
                area.hasSide = true;
                area.side = 0;
                area.hasSearchRadius = true;
                area.searchRadius = 14.1421f;
                areas.push_back(area);
                ai.selectedArea = area.name;
                session.recordStrategicEdit("Add strategic area " + area.name, before);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputTextWithHint("##newName", "free area name", g_newName, sizeof(g_newName));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    ImGui::InputFloat("##newRadius", &g_newRadius, 0.0f, 0.0f, "%g m");
    ImGui::SameLine();
    if (ImGui::Button("New free area at view centre") && g_newName[0] && !ai.find(g_newName))
    {
        const int before = ai.current();
        const FreeCamera& camera = session.camera();
        StrategicArea area;
        area.name = g_newName;
        area.fromControlPoint = false;
        area.radius = g_newRadius;
        const float x = camera.x(), z = camera.z();
        area.corner[0][0] = x - 5.0f;
        area.corner[0][1] = z - 5.0f;
        area.corner[1][0] = x + 5.0f;
        area.corner[1][1] = z + 5.0f;
        float ground = camera.y();
        session.groundHeight(x, z, ground);
        area.height = ground;
        area.hasSide = true;
        areas.push_back(area);
        ai.selectedArea = area.name;
        g_newName[0] = 0;
        session.recordStrategicEdit("Add strategic area " + area.name, before);
    }

    StrategicArea* area = ai.find(ai.selectedArea);
    if (!area)
    {
        ImGui::TextDisabled("Select an area (here, or click its name in the view).");
        ImGui::End();
        return;
    }
    ImGui::SeparatorText(area->name.c_str());
    const std::string name = area->name;
    if (area->fromControlPoint)
    {
        std::string owner = "no control point of the open layer has this id";
        for (size_t i = 0; i < points.size(); ++i)
            if (points[i].id == area->controlPointId)
                owner = "control point " + points[i].name;
        int id = area->controlPointId;
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::InputInt("Control point id", &id, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue))
        {
            const int before = ai.current();
            area->controlPointId = id;
            session.recordStrategicEdit("Set control point of " + name, before);
            area = ai.find(name);
        }
        ImGui::TextDisabled("%s", owner.c_str());
    }
    else
    {
        float corners[4] = { area->corner[0][0], area->corner[0][1], area->corner[1][0], area->corner[1][1] };
        ImGui::SetNextItemWidth(260.0f);
        if (ImGui::InputFloat4("Corners x/z x/z", corners, "%g", ImGuiInputTextFlags_EnterReturnsTrue))
        {
            const int before = ai.current();
            area->corner[0][0] = corners[0];
            area->corner[0][1] = corners[1];
            area->corner[1][0] = corners[2];
            area->corner[1][1] = corners[3];
            session.recordStrategicEdit("Move " + name, before);
            area = ai.find(name);
        }
        float height = area->height;
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::InputFloat("Height", &height, 0.0f, 0.0f, "%g", ImGuiInputTextFlags_EnterReturnsTrue))
        {
            const int before = ai.current();
            area->height = height;
            session.recordStrategicEdit("Set height of " + name, before);
            area = ai.find(name);
        }
    }
    float radius = area->radius;
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::InputFloat("Radius (m)", &radius, 0.0f, 0.0f, "%g", ImGuiInputTextFlags_EnterReturnsTrue))
    {
        const int before = ai.current();
        area->radius = radius;
        session.recordStrategicEdit("Set radius of " + name, before);
        area = ai.find(name);
    }
    static const char* const sides[] = { "0 (both)", "1", "2" };
    int side = area->hasSide ? std::max(0, std::min(2, area->side)) : 0;
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::Combo("Side", &side, sides, 3))
    {
        const int before = ai.current();
        area->hasSide = true;
        area->side = side;
        session.recordStrategicEdit("Set side of " + name, before);
        area = ai.find(name);
    }
    ImGui::SetItemTooltip("The team whose base the area is (0: neither).");
    float search = area->searchRadius;
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::InputFloat("Vehicle search radius", &search, 0.0f, 0.0f, "%g", ImGuiInputTextFlags_EnterReturnsTrue))
    {
        const int before = ai.current();
        area->hasSearchRadius = true;
        area->searchRadius = search;
        session.recordStrategicEdit("Set vehicle search radius of " + name, before);
        area = ai.find(name);
    }

    // Order positions: where squads sent to the area go, per pathfinding map.
    ImGui::SeparatorText("Order positions");
    for (int m = 0; m < 2; ++m)
    {
        ImGui::PushID(m);
        ImGui::TextUnformatted(kMaps[m]);
        ImGui::SameLine(80.0f);
        if (area->hasOrder[m])
        {
            float p[3] = { area->order[m][0], area->order[m][1], area->order[m][2] };
            if (positionField("##order", p))
            {
                const int before = ai.current();
                std::copy(p, p + 3, area->order[m]);
                session.recordStrategicEdit(std::string("Set ") + kMaps[m] + " order position of " + name, before);
                area = ai.find(name);
            }
        }
        else
        {
            ImGui::TextDisabled("none");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(g_orderTool && g_orderMap == m ? "click in the view..." : "Place"))
        {
            g_orderTool = !(g_orderTool && g_orderMap == m);
            g_orderMap = m;
            g_wayPointTool = false;
        }
        if (area->hasOrder[m])
        {
            bool onMesh = true;
            const char* status = meshStatus(session, m, area->order[m], onMesh);
            ImGui::SameLine();
            if (onMesh)
                ImGui::TextDisabled("%s", status);
            else
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s", status);
            if (!onMesh)
                ImGui::SetItemTooltip("Bots sent here fall back to a point they cannot reach and stop short of it.");
        }
        ImGui::PopID();
    }
    ImGui::TextDisabled("Or drag its handle in the view (I: infantry, V: vehicle). Placing and dragging follow the\n"
                        "map's navmesh where the view meets it (balconies, roofs), else the surface under the mouse.");

    // Neighbours: the areas bots move on to from this one.
    ImGui::SeparatorText("Neighbours");
    for (size_t i = 0; i < area->neighbours.size(); ++i)
    {
        ImGui::PushID(static_cast<int>(i));
        const std::string other = area->neighbours[i];
        const StrategicArea* back = ai.find(other);
        ImGui::Text("%s%s", other.c_str(), back && hasNeighbour(*back, name) ? "  (two-way)" : "  (one-way)");
        ImGui::SameLine();
        if (ImGui::SmallButton("x"))
        {
            const int before = ai.current();
            removeNeighbour(*area, other);
            session.recordStrategicEdit("Remove neighbour " + other + " of " + name, before);
            area = ai.find(name);
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    static bool twoWay = true;
    if (ImGui::BeginCombo("##addNeighbour", "Add neighbour..."))
    {
        for (size_t i = 0; i < areas.size(); ++i)
        {
            if (_stricmp(areas[i].name.c_str(), name.c_str()) == 0 || hasNeighbour(*area, areas[i].name))
                continue;
            if (ImGui::Selectable(areas[i].name.c_str()))
            {
                const int before = ai.current();
                const std::string other = areas[i].name;
                area->neighbours.push_back(other);
                if (twoWay && !hasNeighbour(areas[i], name))
                    areas[i].neighbours.push_back(name);
                session.recordStrategicEdit("Add neighbour " + other + " to " + name, before);
                area = ai.find(name);
                break;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Checkbox("two-way", &twoWay);

    // Object type flags.
    ImGui::SeparatorText("Flags");
    for (size_t i = 0; i < area->flags.size(); ++i)
    {
        ImGui::PushID(1000 + static_cast<int>(i));
        ImGui::TextUnformatted(area->flags[i].c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("x"))
        {
            const int before = ai.current();
            area->flags.erase(area->flags.begin() + static_cast<std::ptrdiff_t>(i));
            session.recordStrategicEdit("Remove flag of " + name, before);
            area = ai.find(name);
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    static char flag[48] = "";
    ImGui::SetNextItemWidth(160.0f);
    ImGui::InputTextWithHint("##flag", "ControlPoint", flag, sizeof(flag));
    ImGui::SameLine();
    if (ImGui::SmallButton("Add flag") && flag[0])
    {
        const int before = ai.current();
        area->flags.push_back(flag);
        flag[0] = 0;
        session.recordStrategicEdit("Add flag to " + name, before);
        area = ai.find(name);
    }

    // Waypoints from this area to a neighbour.
    ImGui::SeparatorText("Waypoints");
    std::vector<StrategicWayPoint>& points3 = ai.wayPoints();
    int listed = 0;
    for (size_t i = 0; i < points3.size(); ++i)
    {
        if (_stricmp(points3[i].from.c_str(), name.c_str()) != 0)
            continue;
        ++listed;
        ImGui::PushID(2000 + static_cast<int>(i));
        char label[200];
        std::snprintf(label, sizeof(label), "%s to %s: %.0f / %.0f / %.0f", points3[i].map.c_str(), points3[i].to.c_str(),
                      points3[i].position[0], points3[i].position[1], points3[i].position[2]);
        if (ImGui::Selectable(label, ai.selectedWayPoint == static_cast<int>(i), 0, ImVec2(ImGui::GetContentRegionAvail().x - 30.0f, 0)))
            ai.selectedWayPoint = static_cast<int>(i);
        ImGui::SameLine();
        if (ImGui::SmallButton("x"))
        {
            const int before = ai.current();
            points3.erase(points3.begin() + static_cast<std::ptrdiff_t>(i));
            ai.selectedWayPoint = -1;
            session.recordStrategicEdit("Remove waypoint of " + name, before);
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    if (!listed)
        ImGui::TextDisabled("none");
    const int pickedPoint = ai.selectedWayPoint;
    if (pickedPoint >= 0 && pickedPoint < static_cast<int>(points3.size())
        && _stricmp(points3[static_cast<size_t>(pickedPoint)].from.c_str(), name.c_str()) == 0)
    {
        StrategicWayPoint& point = points3[static_cast<size_t>(pickedPoint)];
        float p[3] = { point.position[0], point.position[1], point.position[2] };
        if (positionField("Selected waypoint", p))
        {
            const int before = ai.current();
            std::copy(p, p + 3, point.position);
            session.recordStrategicEdit("Move waypoint of " + name, before);
        }
        else
        {
            bool onMesh = true;
            const char* status = meshStatus(session, mapOf(point), point.position, onMesh);
            ImGui::SameLine();
            if (onMesh)
                ImGui::TextDisabled("%s", status);
            else
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s", status);
        }
    }
    area = ai.find(name);
    if (area && !area->neighbours.empty())
    {
        ImGui::SetNextItemWidth(110.0f);
        ImGui::Combo("##wpMap", &g_wayPointMap, kMaps, 2);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        if (g_wayPointTo.empty() || !hasNeighbour(*area, g_wayPointTo))
            g_wayPointTo = area->neighbours[0];
        if (ImGui::BeginCombo("##wpTo", ("to " + g_wayPointTo).c_str()))
        {
            for (size_t i = 0; i < area->neighbours.size(); ++i)
                if (ImGui::Selectable(area->neighbours[i].c_str(), area->neighbours[i] == g_wayPointTo))
                    g_wayPointTo = area->neighbours[i];
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button(g_wayPointTool ? "Stop adding" : "Add by clicking"))
        {
            g_wayPointTool = !g_wayPointTool;
            g_orderTool = false;
        }
        ImGui::TextWrapped("Bots leaving this area for a neighbour pick ONE of that pair's waypoints at random (one lying "
                           "towards the neighbour) and pass through it - each is an alternative way, not a path in order. "
                           "Infantry and Vehicle bots use their own. A click adds one at the ground under the cursor.");
    }
    ImGui::Separator();
    if (ImGui::Button("Delete this area"))
    {
        const int before = ai.current();
        for (size_t i = 0; i < areas.size(); ++i)
            removeNeighbour(areas[i], name);
        for (size_t i = points3.size(); i-- > 0;)
            if (_stricmp(points3[i].from.c_str(), name.c_str()) == 0 || _stricmp(points3[i].to.c_str(), name.c_str()) == 0)
                points3.erase(points3.begin() + static_cast<std::ptrdiff_t>(i));
        for (size_t i = 0; i < areas.size(); ++i)
            if (_stricmp(areas[i].name.c_str(), name.c_str()) == 0)
            {
                areas.erase(areas.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
        ai.selectedArea.clear();
        session.recordStrategicEdit("Delete strategic area " + name, before);
    }
    ImGui::End();
}

void drawViewport(EditSession& session, const float viewProjection[16])
{
    if (!session.overlays().strategicAreas)
        return;
    StrategicAreas& ai = session.strategicAreas();
    if (!ai.loaded())
        return;
    ImDrawList* list = ImGui::GetBackgroundDrawList();
    const std::vector<EditSession::LevelObjectRow> points = session.controlPoints();
    const std::vector<StrategicArea>& areas = ai.areas();
    std::vector<float> centres(areas.size() * 3, 0.0f);
    std::vector<bool> placed(areas.size(), false);
    for (size_t i = 0; i < areas.size(); ++i)
        placed[i] = centreOf(areas[i], points, session, &centres[i * 3]);
    // Neighbour links, raised so they clear the ground: an arrow for a one-way link, a plain line for two-way.
    for (size_t i = 0; i < areas.size(); ++i)
    {
        if (!placed[i])
            continue;
        for (size_t k = 0; k < areas[i].neighbours.size(); ++k)
        {
            size_t j = 0;
            while (j < areas.size() && _stricmp(areas[j].name.c_str(), areas[i].neighbours[k].c_str()) != 0)
                ++j;
            if (j == areas.size() || !placed[j])
                continue;
            const bool both = hasNeighbour(areas[j], areas[i].name);
            if (both && j < i)
                continue;
            const float a[3] = { centres[i * 3], centres[i * 3 + 1] + 12.0f, centres[i * 3 + 2] };
            const float b[3] = { centres[j * 3], centres[j * 3 + 1] + 12.0f, centres[j * 3 + 2] };
            ImVec2 sa, sb;
            if (project(viewProjection, a, sa) && project(viewProjection, b, sb))
                arrow(list, sa, sb, both ? IM_COL32(220, 150, 255, 200) : IM_COL32(255, 120, 220, 230), 2.0f, !both);
        }
    }
    // The areas: a circle on the ground, the name over the centre.
    for (size_t i = 0; i < areas.size(); ++i)
    {
        if (!placed[i])
            continue;
        const bool selected = _stricmp(areas[i].name.c_str(), ai.selectedArea.c_str()) == 0;
        const float* c = &centres[i * 3];
        groundCircle(list, session, viewProjection, c, areas[i].radius,
                     selected ? IM_COL32(255, 200, 255, 255) : IM_COL32(200, 120, 255, 170), selected ? 2.5f : 1.5f);
        const float top[3] = { c[0], c[1] + 12.0f, c[2] };
        ImVec2 at;
        if (project(viewProjection, top, at))
        {
            list->AddCircleFilled(at, 4.0f, IM_COL32(200, 120, 255, 230));
            char label[160];
            std::snprintf(label, sizeof(label), "%s%s", areas[i].name.c_str(),
                          areas[i].hasSide && areas[i].side ? (areas[i].side == 1 ? "  [side 1]" : "  [side 2]") : "");
            list->AddText(ImVec2(at.x + 7.0f, at.y - 7.0f), selected ? IM_COL32(255, 220, 255, 255) : IM_COL32(220, 180, 255, 220),
                          label);
        }
        // Order positions: I and V handles.
        for (int m = 0; m < 2; ++m)
        {
            if (!areas[i].hasOrder[m])
                continue;
            float p[3] = { areas[i].order[m][0], areas[i].order[m][1], areas[i].order[m][2] };
            if (g_drag.kind == 1 && g_drag.index == m && _stricmp(g_drag.area.c_str(), areas[i].name.c_str()) == 0 && g_drag.moved)
                std::copy(g_drag.target, g_drag.target + 3, p);
            ImVec2 s;
            if (!project(viewProjection, p, s))
                continue;
            const ImU32 colour = m == 0 ? IM_COL32(90, 230, 120, 240) : IM_COL32(255, 170, 50, 240);
            list->AddRectFilled(ImVec2(s.x - 7.0f, s.y - 7.0f), ImVec2(s.x + 7.0f, s.y + 7.0f), IM_COL32(20, 20, 20, 200));
            list->AddRect(ImVec2(s.x - 7.0f, s.y - 7.0f), ImVec2(s.x + 7.0f, s.y + 7.0f), colour, 0.0f, selected ? 2.5f : 1.5f);
            list->AddText(ImVec2(s.x - 3.5f, s.y - 7.0f), colour, m == 0 ? "I" : "V");
            ImVec2 cs;
            if (selected && project(viewProjection, c, cs))
                list->AddLine(cs, s, colour & IM_COL32(255, 255, 255, 90), 1.0f);
        }
    }
    // Waypoints: each is one alternative way from its area to the neighbour. A bot leaving `from` for `to` picks one
    // of that pair's waypoints at random (one lying towards `to` and nearer to it than the bot), walks there, then
    // on to `to` (AIStrategicArea::orderNormalBot). So each is drawn as its own bent line from -> waypoint -> to.
    const std::vector<StrategicWayPoint>& way = ai.wayPoints();
    for (size_t i = 0; i < way.size(); ++i)
    {
        const bool infantry = _stricmp(way[i].map.c_str(), "Infantry") == 0;
        const bool selectedRoute = _stricmp(way[i].from.c_str(), ai.selectedArea.c_str()) == 0;
        const bool picked = ai.selectedWayPoint == static_cast<int>(i);
        const ImU32 colour = infantry ? IM_COL32(90, 230, 120, selectedRoute ? 220 : 110)
                                      : IM_COL32(255, 170, 50, selectedRoute ? 220 : 110);
        const ImU32 faint = infantry ? IM_COL32(90, 230, 120, selectedRoute ? 110 : 45)
                                     : IM_COL32(255, 170, 50, selectedRoute ? 110 : 45);
        float p[3] = { way[i].position[0], way[i].position[1] + 0.5f, way[i].position[2] };
        if (g_drag.kind == 2 && g_drag.index == static_cast<int>(i) && g_drag.moved)
        {
            std::copy(g_drag.target, g_drag.target + 3, p);
            p[1] += 0.5f;
        }
        ImVec2 s;
        if (!project(viewProjection, p, s))
            continue;
        const StrategicArea* ends[2] = { ai.find(way[i].from), ai.find(way[i].to) };
        for (int e = 0; e < 2; ++e)
        {
            float c[3];
            ImVec2 cs;
            if (ends[e] && centreOf(*ends[e], points, session, c))
            {
                c[1] += 1.0f;
                if (project(viewProjection, c, cs))
                    arrow(list, e == 0 ? cs : s, e == 0 ? s : cs, faint, selectedRoute ? 1.5f : 1.0f, e == 1 && selectedRoute);
            }
        }
        // Infantry a circle, vehicle a square.
        const float size = picked ? 6.0f : 4.0f;
        if (infantry)
            list->AddCircleFilled(s, size, colour);
        else
            list->AddRectFilled(ImVec2(s.x - size, s.y - size), ImVec2(s.x + size, s.y + size), colour);
        if (picked)
            list->AddCircle(s, 10.0f, IM_COL32(255, 255, 255, 230), 16, 2.0f);
    }
    if (g_wayPointTool || g_orderTool)
    {
        const ImGuiIO& io = ImGui::GetIO();
        const char* text = g_wayPointTool ? "Click: add a waypoint (Esc stops)" : "Click: set the order position (Esc stops)";
        list->AddText(ImVec2(io.MousePos.x + 16.0f, io.MousePos.y + 8.0f), IM_COL32(255, 230, 120, 255), text);
    }
}

bool handleViewport(EditSession& session, const float viewProjection[16])
{
    if (!session.overlays().strategicAreas)
    {
        g_wayPointTool = g_orderTool = false;
        g_drag.kind = 0;
        return false;
    }
    StrategicAreas& ai = session.strategicAreas();
    if (!ai.loaded())
        return false;
    const ImGuiIO& io = ImGui::GetIO();
    if (g_drag.kind)
    {
        // The drag follows the ground under the mouse; letting go writes it as one edit.
        float at[3];
        const int map = g_drag.kind == 1 ? g_drag.index
            : (g_drag.index < static_cast<int>(ai.wayPoints().size()) ? mapOf(ai.wayPoints()[static_cast<size_t>(g_drag.index)]) : 0);
        if (mouseTarget(session, map, at))
        {
            std::copy(at, at + 3, g_drag.target);
            g_drag.moved = true;
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            if (g_drag.moved)
            {
                StrategicArea* area = ai.find(g_drag.area);
                if (g_drag.kind == 1 && area)
                {
                    std::copy(g_drag.target, g_drag.target + 3, area->order[g_drag.index]);
                    area->hasOrder[g_drag.index] = true;
                    session.recordStrategicEdit(std::string("Move ") + kMaps[g_drag.index] + " order position of " + g_drag.area,
                                                g_drag.before);
                }
                else if (g_drag.kind == 2 && g_drag.index < static_cast<int>(ai.wayPoints().size()))
                {
                    std::copy(g_drag.target, g_drag.target + 3, ai.wayPoints()[static_cast<size_t>(g_drag.index)].position);
                    session.recordStrategicEdit("Move waypoint of " + g_drag.area, g_drag.before);
                }
            }
            g_drag.kind = 0;
        }
        return true;
    }
    if ((g_wayPointTool || g_orderTool) && ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        g_wayPointTool = g_orderTool = false;
        return true;
    }
    if (pointerOverUi() || !ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        return g_wayPointTool || g_orderTool;
    StrategicArea* selected = ai.find(ai.selectedArea);
    if ((g_wayPointTool || g_orderTool) && selected)
    {
        float at[3];
        if (!mouseTarget(session, g_orderTool ? g_orderMap : g_wayPointMap, at))
            return true;
        const int before = ai.current();
        if (g_orderTool)
        {
            std::copy(at, at + 3, selected->order[g_orderMap]);
            selected->hasOrder[g_orderMap] = true;
            g_orderTool = false;
            session.recordStrategicEdit(std::string("Set ") + kMaps[g_orderMap] + " order position of " + selected->name, before);
        }
        else
        {
            StrategicWayPoint point;
            std::copy(at, at + 3, point.position);
            point.map = kMaps[g_wayPointMap];
            point.from = selected->name;
            point.to = g_wayPointTo;
            // After the route's last point, so the route grows in the order clicked.
            std::vector<StrategicWayPoint>& way = ai.wayPoints();
            size_t insert = way.size();
            for (size_t i = way.size(); i-- > 0;)
                if (way[i].map == point.map && _stricmp(way[i].from.c_str(), point.from.c_str()) == 0
                    && _stricmp(way[i].to.c_str(), point.to.c_str()) == 0)
                {
                    insert = i + 1;
                    break;
                }
            way.insert(way.begin() + static_cast<std::ptrdiff_t>(insert), point);
            ai.selectedWayPoint = static_cast<int>(insert);
            session.recordStrategicEdit("Add waypoint from " + point.from + " to " + point.to, before);
        }
        return true;
    }
    // A press on a handle (order position, waypoint) or an area's name picks it.
    const std::vector<EditSession::LevelObjectRow> points = session.controlPoints();
    const float reach = 10.0f;
    float best = reach * reach;
    Drag hit;
    std::string hitArea;
    const std::vector<StrategicArea>& areas = ai.areas();
    for (size_t i = 0; i < areas.size(); ++i)
    {
        float c[3];
        if (centreOf(areas[i], points, session, c))
        {
            const float top[3] = { c[0], c[1] + 12.0f, c[2] };
            ImVec2 s;
            if (project(viewProjection, top, s))
            {
                const float dx = s.x - io.MousePos.x, dy = s.y - io.MousePos.y;
                if (dx * dx + dy * dy < best)
                {
                    best = dx * dx + dy * dy;
                    hit = Drag();
                    hitArea = areas[i].name;
                }
            }
        }
        for (int m = 0; m < 2; ++m)
        {
            ImVec2 s;
            if (!areas[i].hasOrder[m] || !project(viewProjection, areas[i].order[m], s))
                continue;
            const float dx = s.x - io.MousePos.x, dy = s.y - io.MousePos.y;
            if (dx * dx + dy * dy < best)
            {
                best = dx * dx + dy * dy;
                hit = Drag();
                hit.kind = 1;
                hit.area = areas[i].name;
                hit.index = m;
                hitArea = areas[i].name;
            }
        }
    }
    const std::vector<StrategicWayPoint>& way = ai.wayPoints();
    for (size_t i = 0; i < way.size(); ++i)
    {
        const float p[3] = { way[i].position[0], way[i].position[1] + 0.5f, way[i].position[2] };
        ImVec2 s;
        if (!project(viewProjection, p, s))
            continue;
        const float dx = s.x - io.MousePos.x, dy = s.y - io.MousePos.y;
        if (dx * dx + dy * dy < best)
        {
            best = dx * dx + dy * dy;
            hit = Drag();
            hit.kind = 2;
            hit.area = way[i].from;
            hit.index = static_cast<int>(i);
            hitArea = way[i].from;
        }
    }
    if (hitArea.empty())
        return false;
    ai.selectedArea = hitArea;
    ai.selectedOrder = hit.kind == 1 ? hit.index : -1;
    ai.selectedWayPoint = hit.kind == 2 ? hit.index : -1;
    if (hit.kind)
    {
        g_drag = hit;
        g_drag.before = ai.current();
        g_drag.moved = false;
    }
    return true;
}

} // namespace StrategicAreaTools

} // namespace bf2editor
