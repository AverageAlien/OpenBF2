// Two panels over the open layer's gameplay objects: the Invasion groups (the control points' supplyGroupId order, each
// group's ticket bonus and the spawns each side has there) and the layer checks (spawns off the navmesh, spawners
// without a vehicle, ids that name no control point, objects outside the combat area). A row's Select button selects
// the object and frames it. Editor-only code.
#ifndef BF2EDITOR_APP_LAYERCHECKS_H
#define BF2EDITOR_APP_LAYERCHECKS_H

#include "Framework/EditorConfig.h"

namespace bf2editor
{

class EditSession;

namespace LayerChecks
{

void drawInvasion(EditSession& session, bool* open);
void drawChecks(EditSession& session, bool* open);

} // namespace LayerChecks

} // namespace bf2editor

#endif
