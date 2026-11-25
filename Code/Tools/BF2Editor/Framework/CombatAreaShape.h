// Code/Tools/BF2Editor/Framework/CombatAreaShape.h

// Combat area outlines as the editor draws them: crossing tests for a polygon being clicked point by point, and the
// point order the engine's direction test reads correctly.
//
// The game's inside test (CombatArea::checkPositionPolygon) is crossing parity, so it takes either direction. The AI
// does not: BFObjectEnvironment::getAICombatArea hands the first pathfinding area to the navmesh export
// (GTSData/Meshes/CombatArea.dat) clockwise, reversing it when CombatArea::isClockwise says it is not. That test
// probes one point only: the first edge's midpoint moved by the whole edge length along its left normal, clockwise
// when the probe falls outside. A long first edge or a narrow or concave outline puts the probe on the wrong side
// (retail Highway_Tampa's four-point areas), and the export gets the outline reversed.
// [CHOSEN, not recovered]: a drawn area is stored clockwise, starting at a point whose first edge the probe reads
// right, so the export takes the points as stored. Engine-free (checked in bf2_editor_checks). Editor-only code.
#ifndef BF2EDITOR_FRAMEWORK_COMBATAREASHAPE_H
#define BF2EDITOR_FRAMEWORK_COMBATAREASHAPE_H

#include "Framework/EditorConfig.h"

#include <utility>
#include <vector>

namespace bf2editor
{

typedef std::vector<std::pair<float, float> > AreaOutline;     // (x, z), in order, closed back to the first

// The shoelace sum of the closed outline: negative when it runs clockwise (x right, z up, as the engine's left normal
// (-dz, dx) has it), positive when anticlockwise, about 0 when it encloses nothing.
double areaSignedArea(const AreaOutline& points);

// Whether CombatArea::isClockwise would call the outline clockwise: the same probe and parity test, in float.
// Needs at least two points.
bool areaEngineSaysClockwise(const AreaOutline& points);

// Whether the engine reads the outline's direction right (its probe agrees with the shoelace sign).
bool areaDirectionReadable(const AreaOutline& points);

// Whether segments a-b and c-d touch or cross (a shared end counts; collinear overlap counts).
bool areaSegmentsTouch(const std::pair<float, float>& a, const std::pair<float, float>& b,
                       const std::pair<float, float>& c, const std::pair<float, float>& d);

// Whether an open chain of points would cross itself if `next` were added at its end: the new edge against every
// earlier edge but the one it joins. -1 when it does not, else the index of the first edge it hits.
int areaChainCrossing(const AreaOutline& chain, const std::pair<float, float>& next);

// Whether closing the chain (last point back to the first) crosses one of its edges.
bool areaClosingCrosses(const AreaOutline& chain);

// Whether the closed outline crosses itself anywhere.
bool areaSelfIntersects(const AreaOutline& points);

// Puts a simple outline of three or more points in the order the engine reads right: clockwise, rotated to start at a
// point whose first edge the probe reads right; when no start does, the shortest edge is halved (a point added at
// its middle) until it does. False, with the outline as it came, when it encloses nothing or that fails.
bool areaOrientForEngine(AreaOutline& points);

} // namespace bf2editor

#endif // BF2EDITOR_FRAMEWORK_COMBATAREASHAPE_H
