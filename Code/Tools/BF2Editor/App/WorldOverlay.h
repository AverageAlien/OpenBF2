// World-space editor geometry drawn into the scene with the depth test on, so the terrain and the level's objects
// hide what lies behind them: the water surface, the navmeshes, the spawn points' soldier capsules and the spawn
// rings. The viewport fills it each frame (triangles and lines, colour per vertex, alpha blended); SceneOverlay draws
// it inside renderFrame's scene, just before the ImGui frame, on the depth buffer the world left. [CHOSEN, not
// recovered] Editor-only code.
#ifndef BF2EDITOR_APP_WORLDOVERLAY_H
#define BF2EDITOR_APP_WORLDOVERLAY_H

#include "Framework/EditorConfig.h"

#include <vector>

struct IDirect3DDevice9;

namespace bf2editor
{

struct WorldVertex
{
    float    x, y, z;
    unsigned colour;    // D3DCOLOR: 0xAARRGGBB
};

class WorldOverlay
{
public:
    // This frame's camera: world to view and the projection, row-vector matrices as the renderer's.
    void begin(const float worldToView[16], const float projection[16]);
    void triangle(const float a[3], const float b[3], const float c[3], unsigned colour);
    void line(const float a[3], const float b[3], unsigned colour);
    // Lists the caller keeps (a navmesh's): drawn as they are, until the next begin().
    void triangles(const std::vector<WorldVertex>* list);
    void lines(const std::vector<WorldVertex>* list);
    // Draws what this frame gathered, then forgets it; the device's state is put back afterwards.
    void render(IDirect3DDevice9* device);

private:
    float m_view[16] = {};
    float m_projection[16] = {};
    bool  m_ready = false;
    std::vector<WorldVertex> m_triangles, m_lines;
    std::vector<const std::vector<WorldVertex>*> m_triangleLists, m_lineLists;
};

WorldOverlay& worldOverlay();
// Hands the renderer the draw (RendDX9's editorSetWorldDrawCallback): it then runs inside the world pass, with the
// world's depth. False when the renderer has no such hook (the overlay is then drawn with the UI, depth untested).
bool installWorldOverlayHook();

// 0xAARRGGBB from 0..255 parts.
inline unsigned worldColour(int r, int g, int b, int a)
{
    return (static_cast<unsigned>(a & 255) << 24) | (static_cast<unsigned>(r & 255) << 16)
         | (static_cast<unsigned>(g & 255) << 8) | static_cast<unsigned>(b & 255);
}

} // namespace bf2editor

#endif
