// [CHOSEN, not recovered] OpenBF2 extension, not part of retail BF2: the CCIP sight.
// The launch state and flight model follow the engine's own code paths:
//   FireComp::launchProjectile   - origin, muzzle velocity plus the root's velocity
//   PointPhysicsNode/PhysicsNode - 30 Hz trapezoidal integration, gravity, drag
//   GenericProjectile            - thrust along the facing up to maxSpeed
#include "HudCcip.h"
#if !DEDICATED
#include "BF2HudManager.h"
#include "Ref2System.h"
#include "../Core/Ids.h"
#include "../Game/HudInformationLayer.h"
#include "../Game/Main/BF2ClientSeams.h"
#include "../Game/Objects/IObject.h"
#include "../Game/Objects/IPlayer.h"
#include "../Game/Objects/IWeaponHudComp.h"
#include "../Game/Objects/IWeaponObject.h"
#include "../Game/Objects/ObjectLayoutAccess.h"
#include "../Game/Objects/WeaponHudCompTemplate.h"
#include "../Game/Objects/Base/SimpleObjectTemplate.h"
#include "../Game/Objects/Projectiles/GenericProjectileTemplate.h"
#include "../Math/LocalBoundingBox.h"
#include "../Math/Mat4.h"
#include "../Physics/BasicPhysicsSystem.h"
#include "../Physics/CollisionManager.h"
#include "../Physics/HeightmapCluster.h"
#include "../Physics/ICollisionMesh.h"
#include "../Physics/IPhysicsNode.h"
#include "../Physics/PhysicsType.h"
#include "../RendMisc/IRenderView.h"
#include "../Scene/Object/ObjectTypes/Game/IPlayerControlObject.h"
#include "../Scene/Object/PlayerManager.h"
#include <cmath>
#include <string>

namespace dice { namespace hfe {
extern IBase* g_hudInformationLayer;
namespace world { extern IBase* heightmapCluster; }
namespace hud {

HudCcip* hudCcip = 0;

namespace {
const float kStep = 1.0f / 30.0f;          // the physics tick
// PhysicsNode.cpp g_posDamping: Mesh projectiles lose 1% of their speed every tick, about
// 0.3 * v of drag. Measured in retail: bombs with template drag 0.5 and 50 both slow this way.
const float kMeshDamping = 0.99f;
// Object rays are cast only where the path is this close to the terrain, so a bomb dropped
// from altitude doesn't pay for hundreds of rays through empty sky.
const float kObjectRayHeight = 150.0f;

float length(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

world::IPlayerControlObject* findControlObject(world::IObject* object)
{
    for (world::IObject* node = object; node;
         node = world::object_detail::LayoutAccess::parent(node))
    {
        if (void* const pco = node->queryInterface(world::IID_IPlayerControlObject))
            return static_cast<world::IPlayerControlObject*>(pco);
    }
    return 0;
}

int ccipMode(world::IObject* weapon)
{
    if (!weapon || (weapon->getFlags() & 1))
        return 0;
    world::IObjectComponent* const hud =
        world::object_detail::LayoutAccess::weaponHudComponent(weapon);
    if (!hud)
        return 0;
    const world::WeaponHudInfo* const info = static_cast<world::IWeaponHudComp*>(hud)->getHudInfo();
    return info ? info->ccip : 0;
}

// PhysicsNode::updatePositionalDragAdvanced caps each component so one step can't reverse it.
float clipDrag(float speed, float drag)
{
    if (speed >= 0.0f)
        return speed > drag ? drag : speed;
    return speed <= drag ? drag : speed;
}
}

HudCcip::HudCcip() : m_posX(0), m_posY(0), m_show(false) {}

void HudCcip::registerData()
{
    HudInformationLayer* const layer = static_cast<HudInformationLayer*>(g_hudInformationLayer);
    layer->registerInt("CcipPosX", &m_posX);
    layer->registerInt("CcipPosY", &m_posY);
    layer->registerBool("CcipShow", &m_show);
}

// The selected alternate weapon wins over the primary, so a jet shows its bombs, not its gun.
world::IObject* HudCcip::findWeapon() const
{
    world::IPlayer* const local = world::playerManager->getLocalHumanPlayer();
    if (!local || !local->getIsAlive() || !local->getVehicle())
        return 0;
    world::IPlayerControlObject* const pco = findControlObject(local->getVehicle());
    if (!pco)
        return 0;
    world::IObject* const secondary = pco->getActiveWeapon(1);
    if (ccipMode(secondary))
        return secondary;
    world::IObject* const primary = pco->getActiveWeapon(0);
    if (ccipMode(primary))
        return primary;
    return 0;
}

const HudCcip::ProjectileModel& HudCcip::getModel(world::IObjectTemplate* projectile)
{
    std::map<const world::IObjectTemplate*, ProjectileModel>::iterator found =
        m_models.find(projectile);
    if (found != m_models.end())
        return found->second;

    ProjectileModel model;
    model.physicsType = world::PhysicsType_Point;
    model.dragPerMass = 0.0f;
    model.dragArea = 0.0f;
    model.gravity = static_cast<world::IPhysicsSystem*>(world::basicPhysicsSystem)->getGravity();
    model.acceleration = 0.0f;
    model.maxSpeed = 0.0f;
    model.timeToLive = 10.0f;

    world::SimpleObjectTemplate* const simple = static_cast<world::SimpleObjectTemplate*>(
        projectile->queryImplementation(world::CID_SimpleObjectTemplate));
    if (simple)
    {
        model.physicsType = simple->m_physicsType;
        model.gravity *= simple->m_gravityModifier;
        if (simple->m_drag > 0.0f && simple->m_mass > 0.0f)
            model.dragPerMass = simple->m_drag / simple->m_mass;
        if (model.physicsType == world::PhysicsType_Mesh)
        {
            // PhysicsNode::updatePhysics: area facing the Z axis from the LOD 1 bounding box.
            world::ICollisionMesh* const mesh = world::collisionManager->getCollisionMeshPart(
                simple->getCollisionMesh(), simple->m_collisionPart);
            if (mesh)
            {
                if (mesh->hasLod(1))
                {
                    const LocalBoundingBox* const box = mesh->getBoundingBox(1);
                    const float height = box->m_max.y - box->m_min.y;
                    const float width = box->m_max.x - box->m_min.x;
                    model.dragArea = height * (0.25f * width * 3.1400001f) * simple->m_dragModifier.z;
                }
                mesh->release();
            }
        }
        else
        {
            // PointPhysicsNode::updatePositionalDragSimple with its 0.1 m radius.
            // [INFERRED] RotationalPoint uses the object's bounding radius; 0.1 m stands in.
            model.dragArea = 3.1415927f * 0.1f * 0.1f;
        }
    }

    if (projectile->getClassID() == world::CID_GenericProjectileTemplate)
    {
        world::GenericProjectileTemplate* const generic =
            static_cast<world::GenericProjectileTemplate*>(projectile);
        model.acceleration = generic->m_field1EC;
        model.maxSpeed = generic->m_field1F0;
        const RandomDistribution& ttl = generic->m_timeToLive;
        model.timeToLive = ttl.value;
    }

    return m_models.insert(std::make_pair(projectile, model)).first->second;
}

bool HudCcip::predictImpact(world::IObject* weapon, Vec3& impact)
{
    world::IWeaponObject* const firearm =
        static_cast<world::IWeaponObject*>(weapon->queryInterface(world::IID_IWeaponObject));
    if (!firearm)
        return false;
    world::IObjectTemplate* const projectile = firearm->getProjectileTemplate();
    if (!projectile)
        return false;
    const ProjectileModel& model = getModel(projectile);

    const Mat4& origin = firearm->getProjectileOrigin();
    const Vec3 facing = {origin.m[8], origin.m[9], origin.m[10]};
    const float muzzleVelocity = firearm->getVelocity();
    Vec3 position = {origin.m[12], origin.m[13], origin.m[14]};
    Vec3 velocity = {facing.x * muzzleVelocity, facing.y * muzzleVelocity,
                     facing.z * muzzleVelocity};
    world::IObject* const root = world::getRootParent(weapon);
    if (world::IPhysicsNode* const rootNode = static_cast<world::IPhysicsNode*>(
            world::object_detail::LayoutAccess::physicsNode(root)))
    {
        const Vec3& inherited = rootNode->getAbsolutePositionalSpeed();
        velocity.x += inherited.x;
        velocity.y += inherited.y;
        velocity.z += inherited.z;
    }

    world::IHeightmapCluster* const terrain =
        static_cast<world::IHeightmapCluster*>(world::heightmapCluster);
    const float seaLevel = terrain->getSeaWaterLevel();
    const int steps = static_cast<int>(model.timeToLive * 30.0f);

    for (int step = 0; step < steps; ++step)
    {
        // Point projectiles keep their launch facing; a Mesh projectile (a bomb) is assumed to
        // weathervane into its velocity, so its thrust and drag act along the velocity.
        const float speed = length(velocity);
        Vec3 forward = facing;
        if (model.physicsType == world::PhysicsType_Mesh && speed > 0.001f)
        {
            forward.x = velocity.x / speed;
            forward.y = velocity.y / speed;
            forward.z = velocity.z / speed;
        }

        Vec3 acceleration = {0.0f, model.gravity, 0.0f};
        const float along = velocity.x * forward.x + velocity.y * forward.y
                          + velocity.z * forward.z;
        if (model.acceleration > 0.0f && model.maxSpeed >= along)
        {
            // GenericProjectile sets the acceleration outright, so gravity sits out this tick.
            acceleration.x = forward.x * model.acceleration;
            acceleration.y = forward.y * model.acceleration;
            acceleration.z = forward.z * model.acceleration;
        }
        if (model.dragPerMass > 0.0f)
        {
            if (model.physicsType == world::PhysicsType_Mesh)
            {
                const float scale = speed * model.dragPerMass * model.dragArea * kStep;
                acceleration.x -= clipDrag(velocity.x, velocity.x * scale) * 30.0f;
                acceleration.y -= clipDrag(velocity.y, velocity.y * scale) * 30.0f;
                acceleration.z -= clipDrag(velocity.z, velocity.z * scale) * 30.0f;
            }
            else
            {
                const float scale = model.dragArea * model.dragPerMass;
                acceleration.x -= velocity.x * scale;
                acceleration.y -= velocity.y * scale;
                acceleration.z -= velocity.z * scale;
            }
        }

        // PhysicsNode (Mesh) damps the new speed every tick; PointPhysicsNode doesn't.
        const float damping = model.physicsType == world::PhysicsType_Mesh ? kMeshDamping : 1.0f;
        const Vec3 previous = velocity;
        velocity.x = (velocity.x + kStep * acceleration.x) * damping;
        velocity.y = (velocity.y + kStep * acceleration.y) * damping;
        velocity.z = (velocity.z + kStep * acceleration.z) * damping;
        const Vec3 next = {
            position.x + kStep * 0.5f * (previous.x + velocity.x),
            position.y + kStep * 0.5f * (previous.y + velocity.y),
            position.z + kStep * 0.5f * (previous.z + velocity.z)};

        const float ground = terrain->getHeightInWorldCoords(next.x, next.z);
        const float floor = ground > seaLevel ? ground : seaLevel;
        if (next.y < floor + kObjectRayHeight || position.y < floor + kObjectRayHeight)
        {
            world::IObject* hitObject = 0;
            Vec3 hitPoint = {0.0f, 0.0f, 0.0f};
            if (world::checkIfRayHitAnyObjectOrGround(position, next, weapon, hitObject,
                                                      true, false, true, &hitPoint))
            {
                if (hitPoint.y >= seaLevel)
                {
                    impact = hitPoint;
                    return true;
                }
            }
        }
        if (next.y <= seaLevel)
        {
            const float t = (position.y - seaLevel) / (position.y - next.y);
            impact.x = position.x + (next.x - position.x) * t;
            impact.y = seaLevel;
            impact.z = position.z + (next.z - position.z) * t;
            return true;
        }
        position = next;
    }
    // Nothing hit within the lifetime: show where the projectile ends up, so a helicopter's
    // rocket crosshair stays on screen while the nose points above the horizon.
    impact = position;
    return true;
}

void HudCcip::update(float)
{
    m_show = false;
    world::IObject* const weapon = findWeapon();
    if (!weapon)
        return;
    Vec3 impact;
    if (!predictImpact(weapon, impact))
        return;
    const Vec3 screen = hudManager->worldToScreen(impact);
    menu::Ref2System* const system = hudManager->getSystem();
    if (!(screen.z > -1.0f) || !system)
        return;
    // worldToScreen answers relative to the screen centre (retail 0x7B3080 adds no offset);
    // node positions start at the top-left corner.
    dx9rend::IRenderView* const view =
        static_cast<dx9rend::IRenderView*>(system->getRenderView());
    m_posX = static_cast<int>(std::floor(screen.x + view->getOrthoWidth() * 0.5f + 0.5f));
    m_posY = static_cast<int>(std::floor(screen.y + view->getOrthoHeight() * 0.5f + 0.5f));
    m_show = true;
}

} } }
#endif
