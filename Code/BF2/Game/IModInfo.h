// Mod/map lookup interface. Returned mod and map pointers borrow vector storage.
// No virtual destructor or parseMapDesc slot belongs to this interface.
// [PLATFORM] MSVC groups and reverses overloads differently from the ELF ABI;
// native callers must use typed calls rather than copying numeric slots.

#ifndef GAME_IMODINFO_H     // [INFERRED] guard style, matched to Core/IBase.h
#define GAME_IMODINFO_H

#include <string>
#include <vector>

#include "../Core/IBase.h"

namespace dice {
namespace hfe {

class IModInfo : public IBase
{
public:
    // Map variant metadata read from maptype attributes and element text.
    struct MapType
    {
        MapType() : players(0), ai(false) {}            // [CHOSEN] Zero defaults for variant fields.
                                                        // Each field comes from a maptype attribute except description, which uses element text.
        int          players;        // +0x00  atoi(<maptype players="...">)
        std::string  type;           // +0x04  <maptype type="...">
        std::string  locId;          // +0x08  <maptype locid="...">
        std::string  description;    // +0x0C  the element's text content, not an attribute
        bool         ai;             // +0x10  <maptype ai="..."> - false when the attribute is absent
    };

    // Game mode and its supported map variants; source member names are [INFERRED].
    struct GameMode
    {
        std::string           name;        // +0x00  [UNRESOLVED] name
        std::vector<MapType>  mapTypes;    // +0x04  [UNRESOLVED] name
    };

    // Map metadata: path is the level directory and name is its display name.
    // parseMapDesc fills metadata after directory discovery.
    struct Map
    {
        Map() : gameSpyId(0), plasma(false) {}      // matches init's inline zeroing at 0x82FA1xx.
                                                    // isNightMap remains uninitialized until parseMapDesc.

        std::string            path;        // +0x00  level directory name
        std::string            name;        // +0x04  display name
        std::string            briefing;    // +0x08  <briefing> text
        std::string            briefingLocId; // +0x0C  <briefing locid="...">
        std::string            music;       // +0x10  <music> text
        std::vector<GameMode>  gameModes;   // +0x14  one per <mode> under <modes>
        int                    gameSpyId;   // +0x20  <gsid> - a document attribute, not an element
        bool                   plasma;      // +0x24  <plasma> document attribute, `atoi(v) == 1`
        // parseMapDesc clears this field and resets gameSpyId to -1 before reading metadata.
        bool                   isNightMap;  // +0x25  <isnightmap>
    };

    // Mod metadata loaded from XML. path holds the full mod directory; name is its
    // lowercase leaf. The default single-player map is gulf_of_oman. STL layout varies.
    struct Mod
    {
        std::string               path;          // +0x00  "mods/bf2"
        std::string               name;          // +0x04  "bf2" - lowercased leaf of `path`
        std::string               title;         // +0x08  <title>
        std::string               desc;          // +0x0C  <desc>
        std::string               url;           // +0x10  <url>
        std::string               music;         // +0x14  <music>
        std::string               logo;          // +0x18  <logo>
        std::string               icon;          // +0x1C  <icon>
        std::string               version;       // +0x20  <version>
        std::vector<Map>          maps;          // +0x24
        // Distinct mode names across the mod's maps, deduplicated case-insensitively by parseMapDesc.
        std::vector<std::string>  gameModes;     // +0x30
        std::string               importLevels;  // +0x3C  <importlevels> - the mod whose Levels/
                                                 //        directory is also scanned. See init().
        std::string               spStartupMap;  // +0x40  <sp_startup_map>, default "gulf_of_oman"
    };

    // the interface, IN vtable order. Do not reorder.

    virtual bool             init()                                          = 0;  // +0x10
    virtual std::vector<Mod>* getModList()                                   = 0;  // +0x14
    virtual const Map*       getMap(const std::string& map) const            = 0;  // +0x18
    virtual const Mod*       getCurrentMod() const                           = 0;  // +0x1C
    virtual std::string      getCurrentModPath()                             = 0;  // +0x20
    virtual const Map*       findMapFromCurrentMod(const std::string& map)   = 0;  // +0x24
    virtual const Map*       findMapFromMod(const Mod* mod,
                                            const std::string& map)          = 0;  // +0x28
    virtual const Mod*       getModFromName(const std::string& name)         = 0;  // +0x2C
    virtual const Mod*       getModFromPath(const std::string& path)         = 0;  // +0x30
    virtual const Map*       getMapFromGameSpyId(int id)                     = 0;  // +0x34
    virtual std::string      getMapNameFromMap(const std::string& map)       = 0;  // +0x38
    virtual std::string      getMapPathFromMap(const std::string& map)       = 0;  // +0x3C
    virtual std::string      getMapNameFromMap(const Mod* mod,
                                               const std::string& map)       = 0;  // +0x40
    virtual std::string      getMapPathFromMap(const Mod* mod,
                                               const std::string& map)       = 0;  // +0x44
    virtual void             addNameForGameMode(const std::string& gameMode,
                                                const std::string& name)     = 0;  // +0x48
    virtual std::string      getNameFromGameMode(const std::string& gameMode) = 0; // +0x4C

protected:
    // Non-virtual and protected, exactly as Core/IBase.h argues for IBase: the destructor pair of
    // the concrete class sits at +0x54/+0x58, after parseMapDesc, so it cannot be part of this
    // interface. Nothing ever deletes an IModInfo*; release() does the work.
    ~IModInfo() {}
};

// [CHOSEN, not recovered] OpenBF2 extension: an extra bot game mode is any mode other than the
// retail gpm_cq, gpm_coop and sp1..sp3. It keeps its name in bot games instead of becoming sp1..sp3,
// and the singleplayer page lists its ai="1" map types next to conquest.
inline bool isExtraBotGameMode(const std::string& gameMode)
{
    static const char* const retail[] = { "gpm_cq", "gpm_coop", "sp1", "sp2", "sp3" };
    if (gameMode.empty())
        return false;
    for (unsigned int i = 0; i < sizeof(retail) / sizeof(retail[0]); ++i)
    {
        const char* a = gameMode.c_str();
        const char* b = retail[i];
        while (*a && *b && (*a | 0x20) == (*b | 0x20))
            ++a, ++b;
        if (!*a && !*b)
            return false;
    }
    return true;
}

// [CHOSEN, not recovered] The singleplayer movie hands only a level path and a size back to the
// engine, so its extra-mode rows carry the mode in an alias path "<level>@<mode>". The menu lookups
// and addMapToMapList split it again; retail level paths never contain '@'.
inline bool splitGameModeAlias(const std::string& path, std::string& level, std::string& gameMode)
{
    const std::string::size_type at = path.find('@');
    if (at == std::string::npos || at == 0 || at + 1 >= path.size())
        return false;
    level = path.substr(0, at);
    gameMode = path.substr(at + 1);
    return true;
}

// [CHOSEN, not recovered] The singleplayer page builds an alias row's menu map path as
// "...Levels/<level>@<mode>/Info/<x>_<size>_menuMap.png"; this rewrites it to that mode's own image,
// "...Levels/<level>/Info/<mode>_<size>_menuMap.png". Other paths come back unchanged.
inline std::string resolveGameModeAliasPath(const std::string& path)
{
    std::string result(path);
    const std::string::size_type at = result.find('@');
    if (at == std::string::npos)
        return result;
    const std::string::size_type slash = result.find('/', at);
    if (slash == std::string::npos)
        return result;
    const std::string mode = result.substr(at + 1, slash - at - 1);
    result.erase(at, slash - at);
    const std::string::size_type file = result.rfind('/') + 1;
    const std::string::size_type last = result.rfind('_');
    const std::string::size_type sep =
        (last != std::string::npos && last > file) ? result.rfind('_', last - 1) : std::string::npos;
    if (!mode.empty() && sep != std::string::npos && sep >= file)
        result.replace(file, sep - file, mode);
    return result;
}

} // namespace hfe
} // namespace dice

#endif // GAME_IMODINFO_H
