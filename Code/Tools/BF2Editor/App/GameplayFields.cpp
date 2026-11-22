// Typed fields for the level's gameplay templates (see GameplayFields.h).
#include "Framework/EditorConfig.h"
#include "App/GameplayFields.h"

#include "App/EditSession.h"
#include "App/LevelDocument.h"
#include "Framework/LevelEditorDefs.h"
#include "Framework/LevelObjectFile.h"

#include "Core/Ids.h"
#include "Game/Objects/IObjectTemplate.h"
#include "Game/Objects/IPlayerControlObjectTemplate.h"
#include "Scene/Object/ObjectTypes/Base/ObjectTemplateManager.h"

#include "imgui.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <list>
#include <map>
#include <set>
#include <vector>

namespace bf2editor
{

namespace
{

enum class Type { Int, Float, Bool, Team, Vehicle, Vec3, Vec2, Text };

struct Field
{
    const char* property;   // as the stock files spell it
    int         index;      // the indexed key's first word (setObjectTemplate <team>), or -1
    const char* label;
    Type        type;
    const char* fallback;   // the engine's value when the template has no line
    const char* tip;
};

// [CHOSEN, not recovered] the fields and their order; the defaults are the template constructors' (ObjectSpawnerTemplate,
// SpawnPointTemplate, ControlPointTemplate).
const Field kSpawner[] = {
    { "setObjectTemplate", 1, "Team 1 vehicle", Type::Vehicle, "", "What the spawner makes while team 1 holds it." },
    { "setObjectTemplate", 2, "Team 2 vehicle", Type::Vehicle, "", "What the spawner makes while team 2 holds it." },
    { "minSpawnDelay", -1, "Min spawn delay (s)", Type::Float, "30", "Shortest wait before the next vehicle." },
    { "maxSpawnDelay", -1, "Max spawn delay (s)", Type::Float, "60", "Longest wait before the next vehicle." },
    { "spawnDelayAtStart", -1, "Delay at round start", Type::Bool, "0", "Wait the delay before the first vehicle too." },
    { "TimeToLive", -1, "Abandoned time (s)", Type::Float, "60",
      "How long an empty vehicle away from the spawner lives before it goes and a new one spawns." },
    { "Distance", -1, "Abandoned distance (m)", Type::Float, "50",
      "How far from the spawner an empty vehicle counts as abandoned." },
    { "maxNrOfObjectSpawned", -1, "Max vehicles alive", Type::Int, "1", "Vehicles of this spawner alive at once." },
    { "teamOnVehicle", -1, "Vehicle keeps team", Type::Bool, "0",
      "The vehicle belongs to the team that spawned it (shows its team's colour to the enemy)." },
    { "team", -1, "Spawner team", Type::Team, "0", "The spawner's own team; the control point's owner decides normally." },
    { "damageWhenLost", -1, "Damage when lost", Type::Float, "0", "Damage the vehicle takes when its point is lost." },
};

const Field kSpawnPoint[] = {
    { "setControlPointId", -1, "Control point id", Type::Int, "-1", "The control point whose owner spawns here." },
    { "setOnlyForTeam", -1, "Only for team", Type::Team, "0",
      "OpenBF2: only this team spawns here, whoever holds the point (0 = the point's owner)." },
    { "setGroup", -1, "Spawn group", Type::Int, "0", "Spawn points of one group share one entry on the spawn screen." },
    { "setSpawnPositionOffset", -1, "Position offset", Type::Vec3, "0/0/0", "Where the soldier appears, from the point." },
    { "setOnlyForAI", -1, "Only for bots", Type::Bool, "0", "" },
    { "setOnlyForHuman", -1, "Only for humans", Type::Bool, "0", "" },
    { "setAllowSpawnCloseToVehicle", -1, "Allow near vehicles", Type::Bool, "0",
      "Soldiers may spawn here with a vehicle close by." },
    { "setEnterOnSpawn", -1, "Enter vehicle on spawn", Type::Bool, "0", "A player spawns inside the vehicle here." },
    { "setAIEnterOnSpawn", -1, "Bots enter on spawn", Type::Bool, "0", "A bot spawns inside the vehicle here." },
    { "setSpawnPreventionDelay", -1, "Prevention delay (s)", Type::Float, "0", "" },
    { "setScatterSpawnPositions", -1, "Scatter positions", Type::Bool, "0", "" },
    { "setSpawnAsParaTroper", -1, "Spawn as paratrooper", Type::Bool, "0", "" },
    { "setMinSpawnHeight", -1, "Min spawn height", Type::Float, "-1", "" },
};

const Field kControlPoint[] = {
    { "setControlPointName", -1, "Name players see", Type::Text, "", "The localization key or the name on the map." },
    { "controlPointId", -1, "Control point id", Type::Int, "-1", "Spawn points and spawners name the point by this id." },
    { "team", -1, "Owner at start", Type::Team, "0", "0 = neutral." },
    { "radius", -1, "Capture radius (m)", Type::Float, "1", "" },
    { "timeToGetControl", -1, "Time to capture (s)", Type::Float, "10", "" },
    { "timeToLoseControl", -1, "Time to neutralize (s)", Type::Float, "10", "" },
    { "unableToChangeTeam", -1, "Uncapturable", Type::Bool, "0", "A main base: nobody takes it." },
    { "onlyTakeableByTeam", -1, "Only takeable by", Type::Team, "0", "0 = any team." },
    { "minNrToTakeControl", -1, "Soldiers to capture", Type::Int, "1", "" },
    { "supplyGroupId", -1, "Group (Invasion order)", Type::Int, "-1",
      "Invasion: points are taken group by group, ascending; the open group is the lowest not taken." },
    { "enemyTicketLossWhenCaptured", -1, "Ticket bonus on capture", Type::Int, "0",
      "Invasion: the attackers' bonus tickets when this point's group falls (summed over the group)." },
    { "areaValueTeam1", -1, "AI value for team 1", Type::Int, "0", "How much team 1's bots want the point." },
    { "areaValueTeam2", -1, "AI value for team 2", Type::Int, "0", "How much team 2's bots want the point." },
    { "isUnstrategicControlPoint", -1, "Not strategic (AI)", Type::Bool, "0", "Bots leave it alone." },
    { "showOnMinimap", -1, "Show on minimap", Type::Bool, "1", "" },
    { "loseControlWhenEnemyClose", -1, "Lose when enemy close", Type::Bool, "1", "" },
    { "disableIfEnemyInsideRadius", -1, "Disable spawn if enemy inside", Type::Bool, "0", "" },
    { "isHemisphere", -1, "Hemisphere radius", Type::Bool, "0", "The radius is a half sphere, not a cylinder." },
    { "radiusOffset", -1, "Radius offset", Type::Vec3, "0/0/0", "" },
    { "hoistMinMax", -1, "Flag hoist min/max", Type::Vec2, "0.2/0.9", "" },
};

std::string lowered(std::string text)
{
    for (size_t i = 0; i < text.size(); ++i)
        text[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
    return text;
}

std::string unquoted(const std::string& value)
{
    if (value.size() >= 2 && value[0] == '"' && value[value.size() - 1] == '"')
        return value.substr(1, value.size() - 2);
    return value;
}

std::string keyOf(const Field& field)
{
    return lowered(field.property) + (field.index >= 0 ? " " + std::to_string(field.index) : std::string());
}

// Every vehicle template the engine has loaded (PlayerControlObjects with a vehicle category), and the spawner presets'.
const std::vector<std::string>& vehicleNames(EditSession& session, bool refresh)
{
    static std::vector<std::string> names;
    static bool filled = false;
    if (filled && !refresh)
        return names;
    filled = true;
    names.clear();
    std::set<std::string> seen;
    using namespace dice::hfe::world;
    std::list<IObjectTemplate*>* all = objectTemplateManager ? objectTemplateManager->getGlobalTemplateList() : 0;
    for (std::list<IObjectTemplate*>::const_iterator it = all ? all->begin() : std::list<IObjectTemplate*>::const_iterator();
         all && it != all->end(); ++it)
    {
        IObjectTemplate* t = *it;
        if (!t || t->getClassID() != CID_PlayerControlObjectTemplate)
            continue;
        IPlayerControlObjectTemplate* pco = static_cast<IPlayerControlObjectTemplate*>(t->queryInterface(IID_IPlayerControlObjectTemplate));
        if (!pco || pco->getVehicleCategory() > 3)
            continue;
        if (seen.insert(lowered(t->getName())).second)
            names.push_back(t->getName());
    }
    const std::vector<SpawnerPreset>& presets = session.levelEditorLists().spawnerPresets();
    for (size_t i = 0; i < presets.size(); ++i)
    {
        if (!presets[i].team1.empty() && seen.insert(lowered(presets[i].team1)).second)
            names.push_back(presets[i].team1);
        if (!presets[i].team2.empty() && seen.insert(lowered(presets[i].team2)).second)
            names.push_back(presets[i].team2);
    }
    std::sort(names.begin(), names.end(), [](const std::string& a, const std::string& b) { return lowered(a) < lowered(b); });
    return names;
}

void set(EditSession& session, const std::string& name, const Field& field, const std::string& value)
{
    const std::string written = field.index >= 0 ? std::to_string(field.index) + " " + value : value;
    session.runCommand("EditorTemplate.set " + name + " " + field.property + " \"" + written + "\"", "template");
}

// The field's widget; `present` says whether the template has a line for it (else `value` is the default).
void drawField(EditSession& session, const std::string& name, const Field& field, const std::string& value, bool present)
{
    ImGui::PushID(keyOf(field).c_str());
    if (!present)
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    const float width = 200.0f;
    ImGui::SetNextItemWidth(width);
    switch (field.type)
    {
    case Type::Int:
    {
        int v = std::atoi(value.c_str());
        if (ImGui::InputInt(field.label, &v, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue))
            set(session, name, field, std::to_string(v));
        break;
    }
    case Type::Float:
    {
        float v = static_cast<float>(std::atof(value.c_str()));
        if (ImGui::InputFloat(field.label, &v, 0.0f, 0.0f, "%g", ImGuiInputTextFlags_EnterReturnsTrue))
        {
            char text[32];
            std::snprintf(text, sizeof(text), "%g", v);
            set(session, name, field, text);
        }
        break;
    }
    case Type::Bool:
    {
        bool v = std::atoi(value.c_str()) != 0;
        if (ImGui::Checkbox(field.label, &v))
            set(session, name, field, v ? "1" : "0");
        break;
    }
    case Type::Team:
    {
        static const char* const teams[] = { "0 (none / any)", "1", "2" };
        int v = std::atoi(value.c_str());
        if (v < 0 || v > 2)
            v = 0;
        if (ImGui::Combo(field.label, &v, teams, 3))
            set(session, name, field, std::to_string(v));
        break;
    }
    case Type::Vehicle:
    {
        const std::string shown = value.empty() ? std::string("(none)") : value;
        if (ImGui::BeginCombo(field.label, shown.c_str(), ImGuiComboFlags_HeightLarge))
        {
            static char filter[64] = "";
            static char typed[128] = "";
            if (ImGui::IsWindowAppearing())
            {
                filter[0] = 0;
                std::snprintf(typed, sizeof(typed), "%s", value.c_str());
                ImGui::SetKeyboardFocusHere();
            }
            ImGui::InputTextWithHint("##filter", "filter", filter, sizeof(filter));
            ImGui::SameLine();
            const bool refresh = ImGui::SmallButton("Refresh");
            const std::vector<std::string>& names = vehicleNames(session, refresh);
            const std::string low = lowered(filter);
            ImGui::BeginChild("vehicles", ImVec2(360.0f, 260.0f));
            for (size_t i = 0; i < names.size(); ++i)
            {
                if (!low.empty() && lowered(names[i]).find(low) == std::string::npos)
                    continue;
                if (ImGui::Selectable(names[i].c_str(), _stricmp(names[i].c_str(), value.c_str()) == 0))
                    set(session, name, field, names[i]);
            }
            ImGui::EndChild();
            ImGui::TextDisabled("Not listed (its template not loaded yet)? Type it:");
            ImGui::SetNextItemWidth(260.0f);
            ImGui::InputText("##typed", typed, sizeof(typed));
            ImGui::SameLine();
            if (ImGui::Button("Use") && typed[0])
            {
                set(session, name, field, typed);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndCombo();
        }
        break;
    }
    case Type::Vec3:
    case Type::Vec2:
    {
        const int count = field.type == Type::Vec3 ? 3 : 2;
        float v[3] = { 0.0f, 0.0f, 0.0f };
        std::sscanf(value.c_str(), "%f/%f/%f", &v[0], &v[1], &v[2]);
        const bool changed = count == 3 ? ImGui::InputFloat3(field.label, v, "%g", ImGuiInputTextFlags_EnterReturnsTrue)
                                        : ImGui::InputFloat2(field.label, v, "%g", ImGuiInputTextFlags_EnterReturnsTrue);
        if (changed)
        {
            char text[96];
            if (count == 3)
                std::snprintf(text, sizeof(text), "%g/%g/%g", v[0], v[1], v[2]);
            else
                std::snprintf(text, sizeof(text), "%g/%g", v[0], v[1]);
            set(session, name, field, text);
        }
        break;
    }
    case Type::Text:
    {
        char buffer[128];
        std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
        if (ImGui::InputText(field.label, buffer, sizeof(buffer), ImGuiInputTextFlags_EnterReturnsTrue) && buffer[0])
            set(session, name, field, buffer);
        break;
    }
    }
    if (!present)
        ImGui::PopStyleColor();
    if (field.tip[0] || !present)
        ImGui::SetItemTooltip("%s%s%s", field.tip, field.tip[0] && !present ? "\n" : "",
                              present ? "" : "Not in the file: the engine's default. A change writes the line.");
    if (present && field.type != Type::Vehicle)
    {
        ImGui::SameLine();
        if (ImGui::SmallButton("x"))
        {
            std::string result;
            session.clearTemplateProperty(name, keyOf(field), result);
        }
        ImGui::SetItemTooltip("Take the line out (the default applies once the level loads again).");
    }
    ImGui::PopID();
}

} // namespace

bool hasGameplayFields(const std::string& className)
{
    return _stricmp(className.c_str(), "ObjectSpawner") == 0 || _stricmp(className.c_str(), "SpawnPoint") == 0
        || _stricmp(className.c_str(), "ControlPoint") == 0;
}

void drawGameplayFields(EditSession& session, LevelDocument& document, size_t file, const std::string& name,
                        const std::string& className)
{
    const Field* fields = 0;
    size_t count = 0;
    if (_stricmp(className.c_str(), "ObjectSpawner") == 0)
    {
        fields = kSpawner;
        count = sizeof(kSpawner) / sizeof(kSpawner[0]);
    }
    else if (_stricmp(className.c_str(), "SpawnPoint") == 0)
    {
        fields = kSpawnPoint;
        count = sizeof(kSpawnPoint) / sizeof(kSpawnPoint[0]);
    }
    else if (_stricmp(className.c_str(), "ControlPoint") == 0)
    {
        fields = kControlPoint;
        count = sizeof(kControlPoint) / sizeof(kControlPoint[0]);
    }
    if (!fields)
        return;
    std::map<std::string, std::string> values;
    const std::vector<LevelObjectFile::TemplateLine> lines = document.templateLines(file, name);
    for (size_t i = 0; i < lines.size(); ++i)
    {
        std::string value = unquoted(lines[i].value);
        // An indexed line's value starts with its index.
        if (lines[i].key.find(' ') != std::string::npos)
        {
            const size_t space = value.find_first_of(" \t");
            value = space == std::string::npos ? std::string() : unquoted(value.substr(value.find_first_not_of(" \t", space)));
        }
        values[lines[i].key] = value;
    }
    for (size_t i = 0; i < count; ++i)
    {
        const std::map<std::string, std::string>::const_iterator found = values.find(keyOf(fields[i]));
        const bool present = found != values.end();
        drawField(session, name, fields[i], present ? found->second : std::string(fields[i].fallback), present);
    }
}

} // namespace bf2editor
