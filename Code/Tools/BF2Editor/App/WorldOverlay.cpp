// World-space editor geometry, depth tested (see WorldOverlay.h).
#include "Framework/EditorConfig.h"
#include "App/WorldOverlay.h"

#include <windows.h>
#include <d3d9.h>

#include <algorithm>
#include <cstring>

namespace bf2editor
{

WorldOverlay& worldOverlay()
{
    static WorldOverlay overlay;
    return overlay;
}

namespace
{
void drawFromRenderer(void* device)
{
    worldOverlay().render(static_cast<IDirect3DDevice9*>(device));
}
}

bool installWorldOverlayHook()
{
    typedef void (*SetFn)(void (*)(void*));
    const HMODULE renderer = GetModuleHandleA("RendDX9.dll");
    const SetFn set = renderer ? reinterpret_cast<SetFn>(GetProcAddress(renderer, "editorSetWorldDrawCallback")) : 0;
    if (!set)
        return false;
    set(&drawFromRenderer);
    return true;
}

void WorldOverlay::begin(const float worldToView[16], const float projection[16])
{
    std::memcpy(m_view, worldToView, sizeof(m_view));
    std::memcpy(m_projection, projection, sizeof(m_projection));
    m_ready = true;
    m_triangles.clear();
    m_lines.clear();
    m_triangleLists.clear();
    m_lineLists.clear();
}

void WorldOverlay::triangle(const float a[3], const float b[3], const float c[3], unsigned colour)
{
    const WorldVertex v[3] = { { a[0], a[1], a[2], colour }, { b[0], b[1], b[2], colour }, { c[0], c[1], c[2], colour } };
    m_triangles.insert(m_triangles.end(), v, v + 3);
}

void WorldOverlay::line(const float a[3], const float b[3], unsigned colour)
{
    const WorldVertex v[2] = { { a[0], a[1], a[2], colour }, { b[0], b[1], b[2], colour } };
    m_lines.insert(m_lines.end(), v, v + 2);
}

void WorldOverlay::triangles(const std::vector<WorldVertex>* list)
{
    if (list && !list->empty())
        m_triangleLists.push_back(list);
}

void WorldOverlay::lines(const std::vector<WorldVertex>* list)
{
    if (list && !list->empty())
        m_lineLists.push_back(list);
}

void WorldOverlay::render(IDirect3DDevice9* device)
{
    if (!device || !m_ready)
        return;
    m_ready = false;
    if (m_triangles.empty() && m_lines.empty() && m_triangleLists.empty() && m_lineLists.empty())
        return;
    IDirect3DStateBlock9* saved = 0;
    if (device->CreateStateBlock(D3DSBT_ALL, &saved) != D3D_OK)
        return;
    D3DMATRIX identity;
    std::memset(&identity, 0, sizeof(identity));
    identity._11 = identity._22 = identity._33 = identity._44 = 1.0f;
    device->SetTransform(D3DTS_WORLD, &identity);
    device->SetTransform(D3DTS_VIEW, reinterpret_cast<const D3DMATRIX*>(m_view));
    device->SetTransform(D3DTS_PROJECTION, reinterpret_cast<const D3DMATRIX*>(m_projection));
    device->SetVertexShader(0);
    device->SetPixelShader(0);
    device->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE);
    device->SetTexture(0, 0);
    device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    device->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    device->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    device->SetTextureStageState(1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
    device->SetRenderState(D3DRS_LIGHTING, FALSE);
    device->SetRenderState(D3DRS_FOGENABLE, FALSE);
    device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    device->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    device->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    device->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    device->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    device->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    device->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    device->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    device->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    device->SetRenderState(D3DRS_COLORWRITEENABLE, 0x0F);
    device->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    // The draws in pieces: DrawPrimitiveUP takes at most MaxPrimitiveCount at once.
    const auto draw = [device](D3DPRIMITIVETYPE type, const std::vector<WorldVertex>& list, unsigned perPrimitive) {
        const unsigned chunk = 60000 * perPrimitive;
        for (size_t at = 0; at + perPrimitive <= list.size(); at += chunk)
        {
            const size_t count = std::min<size_t>(chunk, list.size() - at) / perPrimitive;
            if (count)
                device->DrawPrimitiveUP(type, static_cast<UINT>(count), &list[at], sizeof(WorldVertex));
        }
    };
    for (size_t i = 0; i < m_triangleLists.size(); ++i)
        draw(D3DPT_TRIANGLELIST, *m_triangleLists[i], 3);
    draw(D3DPT_TRIANGLELIST, m_triangles, 3);
    for (size_t i = 0; i < m_lineLists.size(); ++i)
        draw(D3DPT_LINELIST, *m_lineLists[i], 2);
    draw(D3DPT_LINELIST, m_lines, 2);
    saved->Apply();
    saved->Release();
}

} // namespace bf2editor
