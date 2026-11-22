// The open game mode's AI strategic areas (GameModes/<mode>/<size>/AI/StrategicAreas.ai): the areas bots attack and
// defend, their neighbours, the order positions squads are sent to and the waypoints between neighbours. The editor
// does not run the AI, so the file is read and written as text in the layout the stock files use; lines this model
// does not know stay with their area. Each edit is a snapshot the history restores (EditorAI.restore), so the
// editor's undo covers it. Editor-only code.
#ifndef BF2EDITOR_APP_STRATEGICAREAS_H
#define BF2EDITOR_APP_STRATEGICAREAS_H

#include "Framework/EditorConfig.h"

#include <string>
#include <vector>

namespace bf2editor
{

class SavePlan;

struct StrategicArea
{
    std::string name;
    bool        created = true;                 // false: the file only sets it up (setActive), another file makes it
    bool        fromControlPoint = true;
    // From a control point: its id and the area's radius (createFromControlPoint <name> <id> <radius>).
    int         controlPointId = -1;
    float       radius = 50.0f;
    // A free area: two corners (x, z), its height and radius (create <name> <x/z> <x/z> <height> <radius>).
    float       corner[2][2] = { { 0.0f, 0.0f }, { 0.0f, 0.0f } };
    float       height = 0.0f;
    std::string category;                       // the create line's optional last argument
    std::string layer = "6";
    std::vector<std::string> neighbours;
    std::vector<std::string> flags;             // addObjectTypeFlag: ControlPoint, ...
    bool        hasOrder[2] = { false, false }; // setOrderPosition Infantry / Vehicle
    float       order[2][3] = { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
    bool        hasSide = false;
    int         side = 0;                       // 0 any, 1, 2
    bool        hasSearchRadius = false;
    float       searchRadius = 10.0f;
    std::vector<std::string> otherLines;        // lines under setActive this model does not know, kept
};

// aiStrategicArea.addWayPoint <x/y/z> <map> <from> <to>: a point of the route a map's bots take from one neighbour
// to the other, in the order the file lists them.
struct StrategicWayPoint
{
    float       position[3] = { 0.0f, 0.0f, 0.0f };
    std::string map = "Infantry";
    std::string from, to;
};

class StrategicAreas
{
public:
    // The file of a game mode's layer: <levelDirectory>GameModes/<mode>/<size>/AI/StrategicAreas.ai. A missing file
    // is an empty model that a save creates once it has areas.
    bool load(const std::string& levelDirectory, const std::string& gameMode, int size, std::string& report);
    bool loaded() const { return m_loaded; }
    const std::string& path() const { return m_path; }

    bool parse(const std::string& text, std::string& error);
    std::string render() const;

    std::vector<StrategicArea>& areas() { return m_areas; }
    const std::vector<StrategicArea>& areas() const { return m_areas; }
    std::vector<StrategicWayPoint>& wayPoints() { return m_wayPoints; }
    const std::vector<StrategicWayPoint>& wayPoints() const { return m_wayPoints; }
    StrategicArea* find(const std::string& name);
    const StrategicArea* find(const std::string& name) const;

    // Snapshots: commit() keeps the model as it now stands and returns its number; restore(n) puts it back.
    int  current() const { return m_current; }
    int  commit();
    bool restore(int snapshot, std::string& error);

    bool modified() const { return m_loaded && render() != m_saved; }
    void planSave(SavePlan& plan);

    // The selection the panel and the viewport share: an area, and in it an order position (0 / 1) or a waypoint.
    std::string selectedArea;
    int         selectedOrder = -1;
    int         selectedWayPoint = -1;

private:
    std::vector<StrategicArea>     m_areas;
    std::vector<StrategicWayPoint> m_wayPoints;
    std::vector<std::string>       m_snapshots;
    std::vector<std::string>       m_headerLines;   // lines before the first area this model does not know
    std::vector<std::string>       m_blockOrder;    // the areas' setActive blocks, in the file's order
    int         m_gap = 0;                          // blank lines between the last area made and the first block
    int         m_current = -1;
    bool        m_loaded = false;
    std::string m_path;
    std::string m_saved;
    bool        m_crlf = true;
};

} // namespace bf2editor

#endif
