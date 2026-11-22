// The open game mode's AI strategic areas as text (see StrategicAreas.h).
#include "Framework/EditorConfig.h"
#include "App/StrategicAreas.h"

#include "Framework/SavePlan.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace bf2editor
{

namespace
{

std::string lowered(std::string text)
{
    for (size_t i = 0; i < text.size(); ++i)
        text[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(text[i])));
    return text;
}

std::vector<std::string> words(const std::string& line)
{
    std::vector<std::string> out;
    std::istringstream in(line);
    std::string word;
    while (in >> word)
        out.push_back(word);
    return out;
}

// "x/y/z" or "x/z" into `count` floats.
bool vector(const std::string& text, float* out, int count)
{
    const char* at = text.c_str();
    for (int i = 0; i < count; ++i)
    {
        char* end = 0;
        out[i] = std::strtof(at, &end);
        if (end == at)
            return false;
        at = end;
        if (i + 1 < count)
        {
            if (*at != '/')
                return false;
            ++at;
        }
    }
    return *at == 0;
}

std::string number(float value)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%g", value);
    return text;
}

std::string vector(const float* v, int count)
{
    std::string text;
    for (int i = 0; i < count; ++i)
        text += (i ? "/" : "") + number(v[i]);
    return text;
}

int mapIndex(const std::string& map)
{
    const std::string low = lowered(map);
    return low == "infantry" ? 0 : low == "vehicle" ? 1 : -1;
}

} // namespace

bool StrategicAreas::load(const std::string& levelDirectory, const std::string& gameMode, int size, std::string& report)
{
    m_areas.clear();
    m_wayPoints.clear();
    m_headerLines.clear();
    m_snapshots.clear();
    m_current = -1;
    m_loaded = false;
    selectedArea.clear();
    selectedOrder = selectedWayPoint = -1;
    m_path = levelDirectory + "GameModes/" + gameMode + "/" + std::to_string(size) + "/AI/StrategicAreas.ai";
    std::ifstream file(m_path.c_str(), std::ios::binary);
    std::string text;
    if (file)
    {
        std::ostringstream all;
        all << file.rdbuf();
        text = all.str();
    }
    std::string error;
    if (!parse(text, error))
    {
        report = m_path + ": " + error;
        m_areas.clear();
        m_wayPoints.clear();
        return false;
    }
    m_loaded = true;
    m_saved = file ? text : render();
    commit();
    char counts[96];
    std::snprintf(counts, sizeof(counts), "%u areas, %u waypoints", static_cast<unsigned>(m_areas.size()),
                  static_cast<unsigned>(m_wayPoints.size()));
    report = m_path + (file ? ": " : " (not there yet): ") + counts;
    return true;
}

StrategicArea* StrategicAreas::find(const std::string& name)
{
    for (size_t i = 0; i < m_areas.size(); ++i)
        if (_stricmp(m_areas[i].name.c_str(), name.c_str()) == 0)
            return &m_areas[i];
    return 0;
}

const StrategicArea* StrategicAreas::find(const std::string& name) const
{
    return const_cast<StrategicAreas*>(this)->find(name);
}

bool StrategicAreas::parse(const std::string& text, std::string& error)
{
    m_areas.clear();
    m_wayPoints.clear();
    m_headerLines.clear();
    m_blockOrder.clear();
    m_gap = 0;
    m_crlf = text.empty() || text.find("\r\n") != std::string::npos;
    int blanks = 0;
    bool afterCreate = false;
    std::istringstream in(text);
    std::string line;
    StrategicArea* active = 0;
    int number = 0;
    while (std::getline(in, line))
    {
        ++number;
        if (!line.empty() && line[line.size() - 1] == '\r')
            line.erase(line.size() - 1);
        const std::vector<std::string> w = words(line);
        if (w.empty())
        {
            ++blanks;
            continue;
        }
        const std::string command = lowered(w[0]);
        char where[32];
        std::snprintf(where, sizeof(where), "line %d: ", number);
        if (command == "aistrategicarea.createfromcontrolpoint" && w.size() >= 4)
        {
            StrategicArea area;
            area.name = w[1];
            area.fromControlPoint = true;
            area.controlPointId = std::atoi(w[2].c_str());
            area.radius = static_cast<float>(std::atof(w[3].c_str()));
            if (w.size() >= 5)
                area.category = w[4];
            m_areas.push_back(area);
            active = &m_areas.back();
            afterCreate = true;
            blanks = 0;
        }
        else if (command == "aistrategicarea.create" && w.size() >= 6)
        {
            StrategicArea area;
            area.name = w[1];
            area.fromControlPoint = false;
            if (!vector(w[2], area.corner[0], 2) || !vector(w[3], area.corner[1], 2))
            {
                error = where + std::string("a corner is not x/z");
                return false;
            }
            area.height = static_cast<float>(std::atof(w[4].c_str()));
            area.radius = static_cast<float>(std::atof(w[5].c_str()));
            if (w.size() >= 7)
                area.category = w[6];
            m_areas.push_back(area);
            active = &m_areas.back();
            afterCreate = true;
            blanks = 0;
        }
        else if (command == "aistrategicarea.layer" && w.size() >= 2 && active)
            active->layer = w[1];
        else if (command == "aistrategicarea.setactive" && w.size() >= 2)
        {
            if (afterCreate)
                m_gap = std::max(0, blanks - 1);
            afterCreate = false;
            active = find(w[1]);
            if (!active)
            {
                StrategicArea area;
                area.name = w[1];
                area.created = false;
                m_areas.push_back(area);
                active = &m_areas.back();
            }
            m_blockOrder.push_back(active->name);
        }
        else if (command == "aistrategicarea.addneighbour" && w.size() >= 2 && active)
            active->neighbours.push_back(w[1]);
        else if (command == "aistrategicarea.addobjecttypeflag" && w.size() >= 2 && active)
            active->flags.push_back(w[1]);
        else if (command == "aistrategicarea.setorderposition" && w.size() >= 3 && active && mapIndex(w[1]) >= 0)
        {
            const int map = mapIndex(w[1]);
            if (!vector(w[2], active->order[map], 3))
            {
                error = where + std::string("the order position is not x/y/z");
                return false;
            }
            active->hasOrder[map] = true;
        }
        else if (command == "aistrategicarea.setside" && w.size() >= 2 && active)
        {
            active->hasSide = true;
            active->side = std::atoi(w[1].c_str());
        }
        else if (command == "aistrategicarea.vehiclesearchradius" && w.size() >= 2 && active)
        {
            active->hasSearchRadius = true;
            active->searchRadius = static_cast<float>(std::atof(w[1].c_str()));
        }
        else if (command == "aistrategicarea.addwaypoint" && w.size() >= 5)
        {
            StrategicWayPoint point;
            if (!vector(w[1], point.position, 3))
            {
                error = where + std::string("the waypoint is not x/y/z");
                return false;
            }
            point.map = w[2];
            point.from = w[3];
            point.to = w[4];
            m_wayPoints.push_back(point);
        }
        else if (command == "rem" && line.find("Create strategic areas") != std::string::npos)
            continue;
        else if (active)
            active->otherLines.push_back(line);
        else
            m_headerLines.push_back(line);
    }
    return true;
}

// The stock layout: every area made (each with its layer and a blank line), the areas' setActive blocks in the order
// the file had them (new areas' after them), then every waypoint.
std::string StrategicAreas::render() const
{
    const char* nl = m_crlf ? "\r\n" : "\n";
    std::string out;
    for (size_t i = 0; i < m_headerLines.size(); ++i)
        out += m_headerLines[i] + nl;
    out += std::string("rem *** Create strategic areas ***") + nl;
    for (size_t i = 0; i < m_areas.size(); ++i)
    {
        const StrategicArea& a = m_areas[i];
        if (!a.created)
            continue;
        if (a.fromControlPoint)
            out += "aiStrategicArea.createFromControlPoint " + a.name + " " + std::to_string(a.controlPointId) + " "
                 + number(a.radius);
        else
            out += "aiStrategicArea.create " + a.name + " " + vector(a.corner[0], 2) + " " + vector(a.corner[1], 2) + " "
                 + number(a.height) + " " + number(a.radius);
        if (!a.category.empty())
            out += " " + a.category;
        out += nl;
        out += "aiStrategicArea.layer " + a.layer + nl + nl;
    }
    for (int i = 0; i < m_gap; ++i)
        out += nl;
    std::vector<const StrategicArea*> blocks;
    for (size_t i = 0; i < m_blockOrder.size(); ++i)
    {
        const StrategicArea* a = find(m_blockOrder[i]);
        if (a && std::find(blocks.begin(), blocks.end(), a) == blocks.end())
            blocks.push_back(a);
    }
    for (size_t i = 0; i < m_areas.size(); ++i)
        if (std::find(blocks.begin(), blocks.end(), &m_areas[i]) == blocks.end())
            blocks.push_back(&m_areas[i]);
    for (size_t i = 0; i < blocks.size(); ++i)
    {
        const StrategicArea& a = *blocks[i];
        out += "aiStrategicArea.setActive " + a.name + nl;
        for (size_t k = 0; k < a.neighbours.size(); ++k)
            out += "AIStrategicArea.addNeighbour " + a.neighbours[k] + nl;
        for (size_t k = 0; k < a.flags.size(); ++k)
            out += "aiStrategicArea.addObjectTypeFlag " + a.flags[k] + nl;
        static const char* const maps[2] = { "Infantry", "Vehicle" };
        for (int m = 0; m < 2; ++m)
            if (a.hasOrder[m])
                out += std::string("AIStrategicArea.setOrderPosition ") + maps[m] + " " + vector(a.order[m], 3) + nl;
        if (a.hasSide)
            out += "aiStrategicArea.setSide " + std::to_string(a.side) + nl;
        if (a.hasSearchRadius)
            out += "aiStrategicArea.vehicleSearchRadius " + number(a.searchRadius) + nl;
        for (size_t k = 0; k < a.otherLines.size(); ++k)
            out += a.otherLines[k] + nl;
        out += nl;
    }
    for (size_t k = 0; k < m_wayPoints.size(); ++k)
    {
        const StrategicWayPoint& p = m_wayPoints[k];
        out += "aiStrategicArea.addWayPoint " + vector(p.position, 3) + " " + p.map + " " + p.from + " " + p.to + nl;
    }
    return out;
}

int StrategicAreas::commit()
{
    // A commit after an undo drops the snapshots past it, as the history drops its redo entries.
    if (m_current + 1 < static_cast<int>(m_snapshots.size()))
        m_snapshots.resize(static_cast<size_t>(m_current + 1));
    m_snapshots.push_back(render());
    m_current = static_cast<int>(m_snapshots.size()) - 1;
    return m_current;
}

bool StrategicAreas::restore(int snapshot, std::string& error)
{
    if (snapshot < 0 || snapshot >= static_cast<int>(m_snapshots.size()))
    {
        error = "no strategic area snapshot " + std::to_string(snapshot);
        return false;
    }
    const std::string selected = selectedArea;
    if (!parse(m_snapshots[static_cast<size_t>(snapshot)], error))
        return false;
    m_current = snapshot;
    selectedArea = find(selected) ? selected : std::string();
    selectedOrder = selectedWayPoint = -1;
    return true;
}

void StrategicAreas::planSave(SavePlan& plan)
{
    if (!modified())
        return;
    SavePart& part = plan.part("AI strategic areas");
    const std::string text = render();
    part.add(m_path, text);
    const std::string path = m_path;
    part.onWritten([this, text]() { m_saved = text; });
    part.onReport([path](const SavePart&) { return "saved " + path; });
}

} // namespace bf2editor
