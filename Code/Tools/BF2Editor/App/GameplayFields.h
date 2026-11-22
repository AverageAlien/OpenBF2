// Typed fields for the gameplay templates a level file defines (control points, spawn points, object spawners): every
// property the stock files and the Invasion mode use, with its engine default when the template has no line for it,
// so a value is picked instead of typed as a raw line. A change is EditorTemplate.set (one undoable edit); a line
// taken away goes back to the default when the level loads again. Editor-only code.
#ifndef BF2EDITOR_APP_GAMEPLAYFIELDS_H
#define BF2EDITOR_APP_GAMEPLAYFIELDS_H

#include "Framework/EditorConfig.h"

#include <string>

namespace bf2editor
{

class EditSession;
class LevelDocument;

// True when `className` gets typed fields (ObjectSpawner, SpawnPoint, ControlPoint).
bool hasGameplayFields(const std::string& className);
void drawGameplayFields(EditSession& session, LevelDocument& document, size_t file, const std::string& name,
                        const std::string& className);

} // namespace bf2editor

#endif
