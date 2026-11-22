// Session-local viewport overlays for gameplay objects, connections, radii, sound ranges and supply lines.
// ViewportTools draws the selected overlays.
#include "Framework/EditorConfig.h"
#include "App/EditSession.h"

#include "App/EditorLevel.h"
#include "App/LevelDocument.h"
#include "Framework/EditorLog.h"

#include "Core/ClassManager.h"
#include "Core/SmartItf.h"
#include "Core/Ids.h"
#include "Core/SettingsRepostitory.h"
#include "Game/Objects/IControlPoint.h"
#include "Game/Objects/IControlPointTemplate.h"
#include "Game/Objects/IPlayerControlObjectTemplate.h"
#include "Game/Objects/ObjectSpawnerTemplate.h"
#include "Game/Objects/IObject.h"
#include "Game/Objects/IObjectSpawner.h"
#include "Game/Objects/IObjectTemplate.h"
#include "Scene/Object/ObjectManager.h"
#include "Scene/Object/ObjectTypes/Base/ObjectTemplateManager.h"

#include <cstdio>
#include <cstdlib>
#include <map>

namespace bf2editor
{

namespace
{

struct OverlayFlag
{
    const char* name;
    bool EditSession::OverlayOptions::*flag;
};

const OverlayFlag kFlags[] = {
    { "controlPoints", &EditSession::OverlayOptions::controlPoints },
    { "spawnPoints", &EditSession::OverlayOptions::spawnPoints },
    { "spawners", &EditSession::OverlayOptions::spawners },
    { "ambient", &EditSession::OverlayOptions::ambient },
    { "connections", &EditSession::OverlayOptions::connections },
    { "controlPointRadius", &EditSession::OverlayOptions::controlPointRadius },
    { "seaLevel", &EditSession::OverlayOptions::seaLevel },
    { "boundingSpheres", &EditSession::OverlayOptions::boundingSpheres },
    { "lightSources", &EditSession::OverlayOptions::lightSources },
    { "soundReachOnGround", &EditSession::OverlayOptions::soundReachOnGround },
    { "selectionBoxes", &EditSession::OverlayOptions::selectionBoxes },
    { "hoverBox", &EditSession::OverlayOptions::hoverBox },
    { "supplyLines", &EditSession::OverlayOptions::supplyLines },
    { "navMeshInfantry", &EditSession::OverlayOptions::navMeshInfantry },
    { "navMeshVehicle", &EditSession::OverlayOptions::navMeshVehicle },
    { "spawnChecks", &EditSession::OverlayOptions::spawnChecks },
    { "soldierFigures", &EditSession::OverlayOptions::soldierFigures },
    { "vehicleGhosts", &EditSession::OverlayOptions::vehicleGhosts },
    { "noFog", &EditSession::OverlayOptions::noFog },
    { "strategicAreas", &EditSession::OverlayOptions::strategicAreas },
};

// PostProductionFrameWork's fog vector (+0x24), the raw call GameLogic::setFogStartEndAndBase makes; the
// commander's overhead view clears the fog the same way (Game/GameLogic/Player.cpp). GSFogStartEndAndBase,
// the level's own value, is left alone.
void setPostProductionFog(const dice::hfe::Vec4& fog)
{
    dice::hfe::IBase* const framework = dice::hfe::classManager->getSingleton("PostProductionFrameWork");
    if (!framework)
        return;
    typedef void (__fastcall* SetStateVec4Fn)(void* self, void* edx, const dice::hfe::Vec4* value);
    void** const vtbl = *reinterpret_cast<void***>(framework);
    reinterpret_cast<SetStateVec4Fn>(vtbl[0x24 / sizeof(void*)])(framework, 0, &fog);
}

} // namespace

EditSession::OverlayOptions::OverlayOptions()
    : controlPoints(true)
    , spawnPoints(true)
    , spawners(true)
    , ambient(true)
    , connections(false)
    , controlPointRadius(false)
    , seaLevel(true)
    , boundingSpheres(false)
    , lightSources(false)
    , soundReachOnGround(false)
    , selectionBoxes(true)
    , hoverBox(false)
    , supplyLines(false)
    , navMeshInfantry(false)
    , navMeshVehicle(false)
    , spawnChecks(true)
    , soldierFigures(true)
    , vehicleGhosts(true)
    , noFog(false)
    , strategicAreas(false)
    , soundMarkers(1)
    , soundReach(1)
{
}

bool EditSession::markerShown(MarkerKind kind) const
{
    if (!m_markersShown)
        return false;
    switch (kind)
    {
    case MarkerControlPoint: return m_overlays.controlPoints;
    case MarkerSpawnPoint:   return m_overlays.spawnPoints;
    case MarkerSpawner:      return m_overlays.spawners;
    case MarkerTrigger:
    case MarkerEffectArea:   return m_overlays.ambient;
    default:                 return false;
    }
}

bool EditSession::setOverlay(const std::string& name, const std::string& value, std::string& result)
{
    char* end = 0;
    const long number = std::strtol(value.c_str(), &end, 10);
    const bool integer = !value.empty() && end && *end == 0;
    for (size_t i = 0; i < sizeof(kFlags) / sizeof(kFlags[0]); ++i)
    {
        if (_stricmp(kFlags[i].name, name.c_str()) != 0)
            continue;
        if (!integer || (number != 0 && number != 1))
        {
            result = "overlay " + name + " is 0 or 1";
            return false;
        }
        m_overlays.*kFlags[i].flag = number != 0;
        result = std::string("overlay ") + kFlags[i].name + (number ? " on" : " off");
        return true;
    }
    if (_stricmp(name.c_str(), "soundMarkers") == 0 || _stricmp(name.c_str(), "soundReach") == 0)
    {
        const bool markers = _stricmp(name.c_str(), "soundMarkers") == 0;
        const long highest = markers ? 3 : 2;
        if (!integer || number < 0 || number > highest)
        {
            result = markers ? "soundMarkers is 0 (off), 1 (small), 2 (medium) or 3 (large)"
                             : "soundReach is 0 (none), 1 (the selected sound's) or 2 (every sound's)";
            return false;
        }
        (markers ? m_overlays.soundMarkers : m_overlays.soundReach) = static_cast<int>(number);
        static const char* const sizes[] = { "off", "small", "medium", "large" };
        static const char* const reach[] = { "none", "the selected sound's", "every sound's" };
        result = markers ? std::string("sound markers ") + sizes[number] : std::string("sound reach: ") + reach[number];
        return true;
    }
    result = "no overlay '" + name + "' (EditorView.overlays lists them)";
    return false;
}

std::string EditSession::overlayList() const
{
    std::string text;
    for (size_t i = 0; i < sizeof(kFlags) / sizeof(kFlags[0]); ++i)
        text += std::string(i ? ", " : "") + kFlags[i].name + ((m_overlays.*kFlags[i].flag) ? " 1" : " 0");
    char numbers[64];
    std::snprintf(numbers, sizeof(numbers), ", soundMarkers %d, soundReach %d", m_overlays.soundMarkers, m_overlays.soundReach);
    return text + numbers;
}

void EditSession::overlayGeometry(std::vector<OverlayLink>& links, std::vector<OverlayCircle>& circles)
{
    using namespace dice::hfe::world;
    links.clear();
    circles.clear();
    if (!objectManager || !m_document)
        return;
    const std::map<unsigned int, IObject*>& roots = objectManager->getAllObjectsFromGrid();
    if (!m_overlayCached || m_overlayRevision != m_history.revision() || m_overlayCount != roots.size()
        || m_overlayVisibility != m_visibilityRevision)
    {
        m_overlayLinks.clear();
        m_overlayCircles.clear();
        // A template's value of a line, from the level file that defines it (cached for this pass).
        std::map<std::string, std::map<std::string, std::string> > values;
        struct Lookup
        {
            static const std::string& of(LevelDocument& document,
                                         std::map<std::string, std::map<std::string, std::string> >& values,
                                         const std::string& templateName, const std::string& key)
            {
                std::map<std::string, std::map<std::string, std::string> >::iterator found = values.find(templateName);
                if (found == values.end())
                {
                    std::map<std::string, std::string>& lines = values[templateName];
                    for (size_t f = 0; f < document.fileCount(); ++f)
                    {
                        const std::vector<LevelObjectFile::TemplateLine> t = document.templateLines(f, templateName);
                        for (size_t l = 0; l < t.size(); ++l)
                            lines[t[l].key] = t[l].value;
                    }
                    found = values.find(templateName);
                }
                static const std::string none;
                const std::map<std::string, std::string>::const_iterator line = found->second.find(key);
                return line == found->second.end() ? none : line->second;
            }
        };
        // controlPointId -> its objects with their layers: every layer's objects are in the view, and each game mode
        // numbers its own control points, so an id joins only objects whose layers meet (layersMeet). Objects of a
        // hidden layer are left out.
        struct Placed
        {
            unsigned object;
            int      layer;
        };
        std::map<int, std::vector<Placed> > pointsById;
        std::vector<std::pair<Placed, int> > linked;
        for (std::map<unsigned int, IObject*>::const_iterator it = roots.begin(); it != roots.end(); ++it)
        {
            IObject* o = it->second;
            const IObjectTemplate* t = o ? o->getTemplate() : 0;
            if (!t)
                continue;
            const int layer = static_cast<int>(o->getLayer());
            if (isLayerHidden(layer))
                continue;
            const Placed placed = { it->first, layer };
            const unsigned classId = t->getClassID();
            if (classId == CID_ControlPointTemplate)
            {
                const std::string& id = Lookup::of(*m_document, values, t->getName(), "controlpointid");
                const std::string& radius = Lookup::of(*m_document, values, t->getName(), "radius");
                if (!id.empty())
                    pointsById[std::atoi(id.c_str())].push_back(placed);
                OverlayCircle circle;
                circle.id = it->first;
                circle.radius = radius.empty() ? 0.0f : static_cast<float>(std::atof(radius.c_str()));
                if (circle.radius > 0.0f)
                    m_overlayCircles.push_back(circle);
                continue;
            }
            IObjectSpawner* spawner = static_cast<IObjectSpawner*>(o->queryInterface(IID_IObjectSpawner));
            if (spawner)
            {
                linked.push_back(std::make_pair(placed, spawner->getControlPointId()));
                continue;
            }
            if (classId == dice::hfe::CID_SpawnPointTemplate)
            {
                const std::string& id = Lookup::of(*m_document, values, t->getName(), "setcontrolpointid");
                if (!id.empty())
                    linked.push_back(std::make_pair(placed, std::atoi(id.c_str())));
            }
        }
        // A spawn point or spawner to each control point of its id whose layer meets its own (one on the Default layer
        // is in every game mode's file, so it may join several).
        for (size_t i = 0; i < linked.size(); ++i)
        {
            const std::map<int, std::vector<Placed> >::const_iterator points = pointsById.find(linked[i].second);
            if (points == pointsById.end())
                continue;
            for (size_t k = 0; k < points->second.size(); ++k)
            {
                if (!layersMeet(linked[i].first.layer, points->second[k].layer))
                    continue;
                OverlayLink link;
                link.from = linked[i].first.object;
                link.to = points->second[k].object;
                m_overlayLinks.push_back(link);
            }
        }
        // Supply groups (E15): the members of one group joined in a chain, and each control point's lines to the
        // members of the groups its teams need.
        // Within a group, each member joins the member before it whose layer meets its own.
        const std::vector<SupplyPoint> supply = supplyPoints();
        std::map<int, std::vector<Placed> > members;
        for (size_t i = 0; i < supply.size(); ++i)
            if (supply[i].group >= 0 && supply[i].object && !isLayerHidden(supply[i].layer))
            {
                const Placed placed = { supply[i].object, supply[i].layer };
                members[supply[i].group].push_back(placed);
            }
        for (std::map<int, std::vector<Placed> >::const_iterator g = members.begin(); g != members.end(); ++g)
            for (size_t i = 1; i < g->second.size(); ++i)
                for (size_t k = i; k-- > 0;)
                    if (layersMeet(g->second[i].layer, g->second[k].layer))
                    {
                        OverlayLink link;
                        link.from = g->second[k].object;
                        link.to = g->second[i].object;
                        link.kind = 1;
                        m_overlayLinks.push_back(link);
                        break;
                    }
        for (size_t i = 0; i < supply.size(); ++i)
            for (int team = 0; team < 2 && supply[i].object && !isLayerHidden(supply[i].layer); ++team)
                for (size_t k = 0; k < supply[i].needs[team].size(); ++k)
                {
                    const std::map<int, std::vector<Placed> >::const_iterator g = members.find(supply[i].needs[team][k]);
                    if (g == members.end())
                        continue;
                    for (size_t m = 0; m < g->second.size(); ++m)
                    {
                        if (g->second[m].object == supply[i].object || !layersMeet(supply[i].layer, g->second[m].layer))
                            continue;
                        OverlayLink link;
                        link.from = supply[i].object;
                        link.to = g->second[m].object;
                        link.kind = 2 + team;
                        m_overlayLinks.push_back(link);
                    }
                }
        m_overlayCached = true;
        m_overlayRevision = m_history.revision();
        m_overlayCount = roots.size();
        m_overlayVisibility = m_visibilityRevision;
    }
    links = m_overlayLinks;
    circles = m_overlayCircles;
}

const NavMesh& EditSession::navMesh()
{
    if (!m_navMesh.tried() && m_document)
    {
        std::string report;
        m_navMesh.load(m_document->levelDirectory(), m_level.name(), report);
        logLine(LogLevel::Info, "navmesh: %s", report.c_str());
    }
    return m_navMesh;
}

bool EditSession::reloadNavMesh(std::string& result)
{
    if (!m_document)
    {
        result = "no level is open";
        return false;
    }
    m_navMesh.load(m_document->levelDirectory(), m_level.name(), result);
    logLine(LogLevel::Info, "navmesh: %s", result.c_str());
    return true;
}

std::string EditSession::spawnerVehicle(dice::hfe::world::IObject* spawner, int team)
{
    using namespace dice::hfe::world;
    IObjectTemplate* t = spawner ? const_cast<IObjectTemplate*>(spawner->getTemplate()) : 0;
    if (!t || t->getClassID() != dice::hfe::world::CID_ObjectSpawnerTemplate)
        return std::string();
    ObjectSpawnerTemplate* spawnerTemplate = static_cast<ObjectSpawnerTemplate*>(t->queryImplementation(dice::hfe::world::CID_ObjectSpawnerTemplate));
    if (!spawnerTemplate)
        return std::string();
    std::string name = spawnerTemplate->getObjectTemplate(static_cast<unsigned>(team));
    if (name.empty())
        name = spawnerTemplate->getObjectTemplate(static_cast<unsigned>(team == 1 ? 2 : 1));
    if (name.empty())
        name = spawnerTemplate->getObjectTemplate(0u);
    return name;
}

EditSession::SpawnCheck EditSession::spawnCheck(dice::hfe::world::IObject* object, float* meshHeight)
{
    using namespace dice::hfe::world;
    const MarkerKind kind = markerKind(object);
    NavMesh::Kind mesh = NavMesh::Infantry;
    // How far under and over the object the mesh may lie: a spawn point stands a little over the ground, a
    // spawner's vehicle drops onto it.
    float below = 2.5f, above = 1.0f;
    if (kind == MarkerSpawner)
    {
        const std::string vehicle = spawnerVehicle(object, 1);
        IObjectTemplate* t = vehicle.empty() || !objectTemplateManager ? 0 : objectTemplateManager->getTemplate(vehicle);
        IPlayerControlObjectTemplate* pco =
            t ? static_cast<IPlayerControlObjectTemplate*>(t->queryInterface(dice::hfe::world::IID_IPlayerControlObjectTemplate)) : 0;
        if (!pco || pco->getVehicleCategory() != 0)     // VCLand
            return SpawnUnchecked;
        // Only vehicles bots drive: tanks, APCs, cars, AA vehicles (VehicleType 0, 1, 3, 4, 17, 18, 20); not
        // artillery, stationary weapons or the commander's assets.
        const int type = pco->getVehicleType();
        if (type != 0 && type != 1 && type != 3 && type != 4 && type != 17 && type != 18 && type != 20)
            return SpawnUnchecked;
        mesh = NavMesh::Vehicle;
        below = 4.0f;
        above = 2.0f;
    }
    else if (kind != MarkerSpawnPoint)
        return SpawnUnchecked;
    const NavMesh& meshes = navMesh();
    if (!meshes.loaded(mesh))
        return SpawnNoMesh;
    const dice::hfe::Mat4& m = object->getAbsoluteTransformation();
    float height = 0.0f;
    if (!meshes.heightAt(mesh, m.m[12], m.m[13], m.m[14], below, above, height))
        return SpawnOffMesh;
    if (meshHeight)
        *meshHeight = height;
    return SpawnOnMesh;
}

StrategicAreas& EditSession::strategicAreas()
{
    if (!m_strategicTried && m_document)
    {
        m_strategicTried = true;
        std::string report;
        m_strategicAreas.load(m_document->levelDirectory(), m_level.gameMode(), m_level.layerSize(), report);
        logLine(m_strategicAreas.loaded() ? LogLevel::Info : LogLevel::Warning, "AI: %s", report.c_str());
    }
    return m_strategicAreas;
}

void EditSession::recordStrategicEdit(const std::string& label, int before)
{
    const int after = m_strategicAreas.commit();
    HistoryEntry entry;
    entry.label = label;
    entry.doScript = "EditorAI.restore " + std::to_string(after) + "\n";
    entry.undoScript = "EditorAI.restore " + std::to_string(before) + "\n";
    // Made already, so the history records it as done.
    m_history.record(entry);
    logLine(LogLevel::Info, "edit: %s", label.c_str());
}

bool EditSession::strategicRestore(int snapshot, std::string& result)
{
    return strategicAreas().restore(snapshot, result);
}

void EditSession::updateFog()
{
    if (m_overlays.noFog)
    {
        // Re-sent every frame: a fog edit (the level settings, a light preset) sets the renderer's fog again.
        const dice::hfe::Vec4 clear = { 0.0f, 100000.0f, 0.0f, 1.0f };
        setPostProductionFog(clear);
        m_fogCleared = true;
    }
    else if (m_fogCleared)
    {
        const dice::hfe::Vec4 zero = { 0.0f, 0.0f, 0.0f, 0.0f };
        if (dice::hfe::settings)
            setPostProductionFog(dice::hfe::settings->Vec4Get(std::string("GSFogStartEndAndBase"), zero));
        m_fogCleared = false;
    }
}

void EditSession::updateControlPointFlags()
{
    using namespace dice::hfe::world;
    if (!objectManager)
        return;
    // Only a server keeps a capturable point's authored team (a client waits for the server's): in the editor such
    // points had no team, so no flag showed. Each point is put back to its template's team, which also follows edits
    // of that team.
    const std::map<unsigned int, IObject*>& roots = objectManager->getAllObjectsFromGrid();
    for (std::map<unsigned int, IObject*>::const_iterator it = roots.begin(); it != roots.end(); ++it)
    {
        if (!it->second || markerKind(it->second) != MarkerControlPoint)
            continue;
        IControlPoint* point = static_cast<IControlPoint*>(it->second->queryInterface(IID_IControlPoint));
        const IObjectTemplate* t = it->second->getTemplate();
        IControlPointTemplate* pointTemplate =
            t ? static_cast<IControlPointTemplate*>(t->queryInterface(IID_IControlPointTemplate)) : 0;
        if (point && pointTemplate && point->getTeam() != pointTemplate->getStartTeam())
            point->reset();
    }
}

} // namespace bf2editor
