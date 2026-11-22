// The Invasion groups and layer checks panels (see LayerChecks.h).
#include "Framework/EditorConfig.h"
#include "App/LayerChecks.h"

#include "App/EditSession.h"
#include "App/LevelDocument.h"
#include "Framework/LevelObjectFile.h"

#include "Game/Objects/IObject.h"
#include "Game/Objects/IObjectTemplate.h"
#include "Scene/Object/ObjectManager.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace bf2editor
{

namespace LayerChecks
{

namespace
{

using dice::hfe::world::IObject;

struct Item
{
    unsigned                           id = 0;
    EditSession::MarkerKind            kind = EditSession::MarkerNone;
    std::string                        name;
    float                              position[3] = { 0.0f, 0.0f, 0.0f };
    std::map<std::string, std::string> values;      // template key -> value, unquoted
    int                                objectControlPoint = -1;     // a spawner object's Object.setControlPointId
    int value(const char* key, int fallback) const
    {
        std::map<std::string, std::string>::const_iterator it = values.find(key);
        return it == values.end() ? fallback : std::atoi(it->second.c_str());
    }
    std::string text(const char* key) const
    {
        std::map<std::string, std::string>::const_iterator it = values.find(key);
        return it == values.end() ? std::string() : it->second;
    }
};

std::string unquoted(const std::string& value)
{
    if (value.size() >= 2 && value[0] == '"' && value[value.size() - 1] == '"')
        return value.substr(1, value.size() - 2);
    return value;
}

// The gameplay objects of the open layer (its spawners, spawn points and control points), with their templates' lines.
std::vector<Item> gather(EditSession& session)
{
    std::vector<Item> items;
    LevelDocument* document = session.document();
    if (!document || !dice::hfe::world::objectManager)
        return items;
    const std::map<unsigned int, IObject*>& roots = dice::hfe::world::objectManager->getAllObjectsFromGrid();
    for (std::map<unsigned int, IObject*>::const_iterator it = roots.begin(); it != roots.end(); ++it)
    {
        IObject* object = it->second;
        const EditSession::MarkerKind kind = session.markerKind(object);
        if (kind != EditSession::MarkerSpawner && kind != EditSession::MarkerSpawnPoint && kind != EditSession::MarkerControlPoint)
            continue;
        Item item;
        item.id = it->first;
        item.kind = kind;
        item.name = object->getTemplate()->getName();
        const dice::hfe::Mat4& m = object->getAbsoluteTransformation();
        item.position[0] = m.m[12];
        item.position[1] = m.m[13];
        item.position[2] = m.m[14];
        size_t file = 0;
        std::string className;
        if (document->templateFile(item.name, file, className))
        {
            const std::vector<LevelObjectFile::TemplateLine> lines = document->templateLines(file, item.name);
            for (size_t i = 0; i < lines.size(); ++i)
                item.values[lines[i].key] = unquoted(lines[i].value);
        }
        if (kind == EditSession::MarkerSpawner)
            item.objectControlPoint = EditSession::propertiesOf(object).controlPointId;
        items.push_back(item);
    }
    return items;
}

void selectButton(EditSession& session, unsigned id)
{
    ImGui::PushID(static_cast<int>(id));
    if (ImGui::SmallButton("Select"))
    {
        session.runCommand("EditorSelection.select " + std::to_string(id), "select");
        session.runCommand("EditorCamera.frameSelection", "camera");
    }
    ImGui::PopID();
}

std::string vehicleOf(const Item& spawner, int team)
{
    const std::string value = spawner.text(("setobjecttemplate " + std::to_string(team)).c_str());
    const size_t space = value.find_first_of(" \t");
    return space == std::string::npos ? std::string() : unquoted(value.substr(value.find_first_not_of(" \t", space)));
}

int spawnPointPoint(const Item& item)
{
    return item.kind == EditSession::MarkerSpawner ? item.objectControlPoint : item.value("setcontrolpointid", -1);
}

bool pointInPolygon(const std::vector<std::pair<float, float> >& polygon, float x, float z)
{
    bool inside = false;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++)
    {
        const float xi = polygon[i].first, zi = polygon[i].second, xj = polygon[j].first, zj = polygon[j].second;
        if ((zi > z) != (zj > z) && x < (xj - xi) * (z - zi) / (zj - zi) + xi)
            inside = !inside;
    }
    return inside;
}

} // namespace

void drawInvasion(EditSession& session, bool* open)
{
    if (!ImGui::Begin("Invasion groups", open))
    {
        ImGui::End();
        return;
    }
    const std::vector<Item> items = gather(session);
    std::vector<const Item*> points;
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].kind == EditSession::MarkerControlPoint)
            points.push_back(&items[i]);
    // Groups in the order gpm_invasion.py takes them: supplyGroupId ascending, those above 0.
    std::map<int, std::vector<const Item*> > groups;
    for (size_t i = 0; i < points.size(); ++i)
        if (points[i]->value("supplygroupid", -1) > 0)
            groups[points[i]->value("supplygroupid", -1)].push_back(points[i]);
    if (groups.empty())
    {
        ImGui::TextWrapped("No control point has a group (supplyGroupId above 0) in this layer. Set Group (Invasion order) "
                           "on the objectives in Properties, or below.");
    }
    const int defenders = groups.empty() ? 0 : groups.begin()->second[0]->value("team", 0);
    const int attackers = defenders == 1 ? 2 : defenders == 2 ? 1 : 0;
    if (!groups.empty())
        ImGui::Text("Defenders: team %d (the first group's owner). Attackers: team %d.", defenders, attackers);
    ImGui::TextDisabled("Start tickets are the layer's gameLogic.setDefaultNumberOfTicketsEx lines (not edited here).");

    int number = 0;
    for (std::map<int, std::vector<const Item*> >::const_iterator g = groups.begin(); g != groups.end(); ++g)
    {
        ++number;
        int bonus = 0;
        std::vector<int> ids;
        for (size_t k = 0; k < g->second.size(); ++k)
        {
            bonus += g->second[k]->value("enemyticketlosswhencaptured", 0);
            ids.push_back(g->second[k]->value("controlpointid", -1));
        }
        int defend = 0, attack = 0, either = 0, spawners = 0;
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (items[i].kind == EditSession::MarkerControlPoint
                || std::find(ids.begin(), ids.end(), spawnPointPoint(items[i])) == ids.end())
                continue;
            if (items[i].kind == EditSession::MarkerSpawner)
            {
                ++spawners;
                continue;
            }
            const int team = items[i].value("setonlyforteam", 0);
            (team == defenders ? defend : team == attackers && attackers ? attack : either) += 1;
        }
        char header[160];
        std::snprintf(header, sizeof(header), "Group %d (supplyGroupId %d)%s - bonus %d tickets###group%d", number, g->first,
                      number == static_cast<int>(groups.size()) ? ", last" : "", bonus, g->first);
        if (!ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen))
            continue;
        ImGui::Text("Spawn points: %d defender-only, %d attacker-only, %d either; %d spawners", defend, attack, either, spawners);
        if (!defend)
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.3f, 1.0f), "No defender-only spawn point: defenders spawn here only on "
                               "points they hold.");
        if (!attack && number < static_cast<int>(groups.size()) + 1)
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "No attacker-only spawn point: attackers get no forward spawn "
                               "here once the group falls.");
        for (size_t k = 0; k < g->second.size(); ++k)
        {
            const Item& point = *g->second[k];
            ImGui::PushID(static_cast<int>(point.id));
            ImGui::BulletText("%s (id %d, owner team %d)", point.name.c_str(), point.value("controlpointid", -1),
                              point.value("team", 0));
            ImGui::SameLine();
            selectButton(session, point.id);
            ImGui::Indent();
            int group = g->first;
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::InputInt("group", &group, 1, 1, ImGuiInputTextFlags_EnterReturnsTrue))
                session.runCommand("EditorTemplate.set " + point.name + " supplyGroupId \"" + std::to_string(group) + "\"",
                                   "template");
            ImGui::SameLine();
            int tickets = point.value("enemyticketlosswhencaptured", 0);
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::InputInt("bonus tickets", &tickets, 10, 50, ImGuiInputTextFlags_EnterReturnsTrue))
                session.runCommand("EditorTemplate.set " + point.name + " enemyTicketLossWhenCaptured \"" + std::to_string(tickets)
                                   + "\"", "template");
            ImGui::Unindent();
            ImGui::PopID();
        }
    }
    // Points in no group: the main bases, and objectives still to place in the order.
    ImGui::SeparatorText("Control points in no group");
    for (size_t i = 0; i < points.size(); ++i)
    {
        const Item& point = *points[i];
        if (point.value("supplygroupid", -1) > 0)
            continue;
        ImGui::PushID(static_cast<int>(point.id));
        ImGui::BulletText("%s (id %d, team %d)%s", point.name.c_str(), point.value("controlpointid", -1), point.value("team", 0),
                          point.value("unabletochangeteam", 0) ? "  main base" : "");
        ImGui::SameLine();
        selectButton(session, point.id);
        ImGui::SameLine();
        if (ImGui::SmallButton("Make it the next group"))
        {
            const int next = groups.empty() ? 1 : groups.rbegin()->first + 1;
            session.runCommand("EditorTemplate.set " + point.name + " supplyGroupId \"" + std::to_string(next) + "\"", "template");
        }
        ImGui::PopID();
    }
    ImGui::End();
}

void drawChecks(EditSession& session, bool* open)
{
    if (!ImGui::Begin("Layer checks", open))
    {
        ImGui::End();
        return;
    }
    const std::vector<Item> items = gather(session);
    std::vector<int> pointIds;
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].kind == EditSession::MarkerControlPoint)
            pointIds.push_back(items[i].value("controlpointid", -1));
    // The combat areas every team uses (team 0, not an exclusion).
    std::vector<LevelObjectFile::CombatAreaData> areas = EditSession::combatAreas();
    struct Problem
    {
        unsigned    id;
        std::string text;
        int         severity;   // 0 error, 1 warning
    };
    std::vector<Problem> problems;
    std::map<int, int> spawnsOfPoint;
    for (size_t i = 0; i < items.size(); ++i)
    {
        const Item& item = items[i];
        IObject* object = EditSession::object(item.id);
        if (item.kind == EditSession::MarkerSpawnPoint || item.kind == EditSession::MarkerSpawner)
        {
            const int point = spawnPointPoint(item);
            if (item.kind == EditSession::MarkerSpawnPoint)
                ++spawnsOfPoint[point];
            if (std::find(pointIds.begin(), pointIds.end(), point) == pointIds.end())
                problems.push_back({ item.id, item.name + ": its control point id " + std::to_string(point)
                                                  + " names no control point of this layer", 0 });
            const EditSession::SpawnCheck check = object ? session.spawnCheck(object) : EditSession::SpawnUnchecked;
            if (check == EditSession::SpawnOffMesh)
                problems.push_back({ item.id, item.name + (item.kind == EditSession::MarkerSpawnPoint
                                                               ? ": off the infantry navmesh (bots spawning here may stand still)"
                                                               : ": off the vehicle navmesh (bots will not drive its vehicle)"),
                                     1 });
        }
        if (item.kind == EditSession::MarkerSpawner && vehicleOf(item, 1).empty() && vehicleOf(item, 2).empty())
            problems.push_back({ item.id, item.name + ": no vehicle for either team", 0 });
        bool outside = false;
        bool anyArea = false;
        for (size_t a = 0; a < areas.size(); ++a)
        {
            if (areas[a].team != 0 || areas[a].inverted || areas[a].points.size() < 3)
                continue;
            anyArea = true;
            outside = !pointInPolygon(areas[a].points, item.position[0], item.position[2]);
            if (!outside)
                break;
        }
        if (anyArea && outside)
            problems.push_back({ item.id, item.name + ": outside the combat area", 1 });
    }
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].kind == EditSession::MarkerControlPoint && !spawnsOfPoint[items[i].value("controlpointid", -1)]
            && !items[i].value("unabletochangeteam", 0))
            problems.push_back({ items[i].id, items[i].name + ": no spawn point names this control point", 1 });
    if (session.navMesh().tried() && !session.navMesh().loaded(NavMesh::Infantry))
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "The level has no readable infantry navmesh: spawn points are "
                           "not checked against it.");
    if (problems.empty())
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "No problems found among %u gameplay objects.",
                           static_cast<unsigned>(items.size()));
    for (size_t i = 0; i < problems.size(); ++i)
    {
        ImGui::PushID(static_cast<int>(i));
        selectButton(session, problems[i].id);
        ImGui::SameLine();
        ImGui::TextColored(problems[i].severity == 0 ? ImVec4(1.0f, 0.45f, 0.4f, 1.0f) : ImVec4(1.0f, 0.8f, 0.35f, 1.0f), "%s",
                           problems[i].text.c_str());
        ImGui::PopID();
    }
    ImGui::End();
}

} // namespace LayerChecks

} // namespace bf2editor
