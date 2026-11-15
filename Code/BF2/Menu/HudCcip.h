// [CHOSEN, not recovered] OpenBF2 extension, not part of retail BF2: the CCIP (continuously
// computed impact point) sight. Weapons opt in with `ObjectTemplate.weaponHud.ccip 1`.
#ifndef MENU_HUDCCIP_H
#define MENU_HUDCCIP_H
#include "../Core/BuildConfig.h"
#if !DEDICATED
#include "../Math/Vec3.h"
#include <map>
namespace dice { namespace hfe {
namespace world { class IObject; class IObjectTemplate; }
namespace hud {

// Predicts where the local player's selected CCIP weapon would hit and publishes the point in
// HUD coordinates as CcipPosX/CcipPosY (ints) with CcipShow (bool). HUD scripts bind a reticle
// with setNodePosVariable and centre it with setNodeOffset.
class HudCcip
{
public:
    HudCcip();
    // Registers the HUD variables; must run before the HUD scripts bind them.
    void registerData();
    void update(float delta);

private:
    struct ProjectileModel
    {
        int physicsType;      // world::PhysicsType
        float dragPerMass;    // template drag / mass
        float dragArea;       // Point: pi * 0.1^2; Mesh: the nose-on area of collision LOD 1
        float gravity;        // physics gravity * gravityModifier
        float acceleration;   // projectile thrust along its facing
        float maxSpeed;       // thrust stops above this speed along the facing
        float timeToLive;
    };

    world::IObject* findWeapon() const;
    const ProjectileModel& getModel(world::IObjectTemplate* projectile);
    bool predictImpact(world::IObject* weapon, Vec3& impact);

    int m_posX;
    int m_posY;
    bool m_show;
    std::map<const world::IObjectTemplate*, ProjectileModel> m_models;
};

extern HudCcip* hudCcip;

} } }
#endif
#endif
