// The AI strategic areas' panel and viewport tools (App/StrategicAreas): the areas as circles on the ground, their
// neighbour links, the order positions and waypoints as handles that a drag moves, and a click that adds a waypoint.
// Every edit is one history entry (EditSession::recordStrategicEdit). Editor-only code.
#ifndef BF2EDITOR_APP_STRATEGICAREATOOLS_H
#define BF2EDITOR_APP_STRATEGICAREATOOLS_H

#include "Framework/EditorConfig.h"

namespace bf2editor
{

class EditSession;

namespace StrategicAreaTools
{

void drawPanel(EditSession& session, bool* open);
// The overlay (OverlayOptions::strategicAreas); drawn whenever it is on.
void drawViewport(EditSession& session, const float viewProjection[16]);
// The handles' presses and drags and the waypoint tool's clicks; true when they took the mouse this frame.
bool handleViewport(EditSession& session, const float viewProjection[16]);

} // namespace StrategicAreaTools

} // namespace bf2editor

#endif
