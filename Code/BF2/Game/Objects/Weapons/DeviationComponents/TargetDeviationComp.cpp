// Path:     [INFERRED] beside the attested DeviationComponents family.
// Contents: complete TargetDeviationComp runtime/template pair, recovered from the BF2 1.50 ELF
// and cross-checked against bf2_w32ded.exe.

#include "TargetDeviationComp.h"

#include <cmath>
#include <sstream>
#include <string>

#include "../../ObjectLayoutAccess.h"
#include "../../IObject.h"
#include "../../../../Core/IBase.h"
#include "../../../../Core/Ids.h"
#include "../../../../Core/System.h"
#include "../../../../IO/Stream/IStream.h"
#include "../../../../IO/Input/PlayerInput.h"
#include "../../../../Physics/HeightmapCluster.h"
#include "../../../../Physics/IPhysicsNode.h"

namespace dice {
namespace hfe {
namespace world {

extern IBase* heightmapCluster;

namespace {

float rawUnit(Random& random)
{
    const unsigned int raw = ((random.getRaw() >> 7) | 1u) + 1u;
    return static_cast<float>(static_cast<int>(raw)) * 0.000000059604645f;
}

} // anonymous namespace

TargetDeviationComp::TargetDeviationComp(TargetDeviationCompTemplate* objectTemplate)
    : DeviationComp(objectTemplate)
    , m_targetRefCount(1)
    , m_targetRandom()
    , m_aiDeviation(0.0f)
    , m_fixedDeviations()
    , m_targetTemplate(objectTemplate)
{
}

TargetDeviationComp::~TargetDeviationComp()
{
}

void TargetDeviationComp::addRef() { ++m_targetRefCount; }
int TargetDeviationComp::getRef() { return m_targetRefCount; }

int TargetDeviationComp::release()
{
    const int remaining = --m_targetRefCount;
    if (remaining == 0 && this)
        delete this;
    return remaining;
}

void* TargetDeviationComp::queryInterface(unsigned int iid) const
{
    TargetDeviationComp* const self = const_cast<TargetDeviationComp*>(this);
    if (iid == IID_IBase || iid == IID_IObjectComponent)
        return self;
    if (iid == IID_IWeaponComp || iid == IID_IWeaponDeviationComp)
        return static_cast<IWeaponDeviationComp*>(self);
    return 0;
}

unsigned int TargetDeviationComp::getInterfaceID() const
{
    return IID_IWeaponDeviationComp;
}

void TargetDeviationComp::updateComponent(float, unsigned int) {}
void TargetDeviationComp::updateDeviation(io::PlayerInput, bool, bool) {}

// . The seed is rebuilt for each burst member from the weapon object's id, current
// tick and caller index. Y is snapped to terrain at the resulting x/z, exactly as shipped.
// [CHOSEN, not recovered] OpenBF2 change: retail took x evenly over the diameter and z evenly over
// the chord at that x, which crowds impacts towards the x ends of the circle. The two samples now
// give a radius (square root, for an even density) and an angle, so impacts cover the disc evenly.
Vec3 TargetDeviationComp::deviatePosition(Vec3& position, int projectileIndex)
{
    IObject* const object = static_cast<IObject*>(
        m_initObject->queryInterface(IID_IObject));
    const int seed = projectileIndex
                   + (WorldPref::getTickCount() & 0x3FF)
                   + (static_cast<int>(object->getObjectId()) & 0x3FF)
                   + 3323;
    m_targetRandom.m_seed = static_cast<unsigned int>(seed) & 0x7FFFFFFFu;

    const float distance = std::sqrt(rawUnit(m_targetRandom)) * m_targetTemplate->getRadius();
    const float angle = rawUnit(m_targetRandom) * 6.2831853f;
    const float x = position.x + std::cos(angle) * distance;
    const float z = position.z + std::sin(angle) * distance;

    IHeightmapCluster* const cluster = static_cast<IHeightmapCluster*>(heightmapCluster);
    const float heightDelta = cluster->getHeightInWorldCoords(x, z) - position.y;
    Vec3 result = { x, position.y + heightDelta, z };
    return result;
}

void TargetDeviationComp::setProjectileDeviationFixed(Vec3& direction, int projectileIndex)
{
    m_fixedDeviations[projectileIndex] = direction;
}

void TargetDeviationComp::postFire(WeaponFireEvent& event)
{
    if (!event.projectile)
        return;

    // Unlike DeviationComp's common body, retail has no null guard for this physics node.
    IPhysicsNode* const node = static_cast<IPhysicsNode*>(
        object_detail::LayoutAccess::physicsNode(event.projectile));
    const Vec3& velocity = node->getPositionalSpeed();
    const float speed = std::sqrt(velocity.x * velocity.x
                                + velocity.y * velocity.y
                                + velocity.z * velocity.z);
    const Vec3& direction = m_fixedDeviations[event.sequenceNumber];
    Vec3 redirected = { direction.x * speed, direction.y * speed, direction.z * speed };
    node->setPositionalSpeed(redirected);
}

float TargetDeviationComp::getMaxDeviation() const
{
    return m_targetTemplate->getFireDevMax() + m_targetTemplate->getMinDev();
}

void TargetDeviationComp::setAIDeviation(float deviation)
{
    m_aiDeviation = deviation;
}

TargetDeviationCompTemplate::TargetDeviationCompTemplate()
    : DeviationCompTemplate()
    , m_targetRefCount(1)
    , m_radius(0.0f)
{
}

void TargetDeviationCompTemplate::addRef() { ++m_targetRefCount; }
int TargetDeviationCompTemplate::getRef() { return m_targetRefCount; }

int TargetDeviationCompTemplate::release()
{
    const int remaining = --m_targetRefCount;
    if (remaining == 0 && this)
        delete this;
    return remaining;
}

void* TargetDeviationCompTemplate::queryInterface(unsigned int iid) const
{
    TargetDeviationCompTemplate* const self =
        const_cast<TargetDeviationCompTemplate*>(this);
    if (iid == IID_IBase || iid == IID_IObjectTemplateComponent
        || iid == IID_IWeaponCompTemplate)
        return self;
    if (iid == io::IID_IConsoleSaveable)
        return static_cast<io::IConsoleSaveable*>(self);
    return 0;
}

unsigned int TargetDeviationCompTemplate::getClassID() const
{
    return CID_TargetDeviationCompTemplate;
}

unsigned int TargetDeviationCompTemplate::getInterfaceID()
{
    return IID_IWeaponDeviationCompTemplate;
}

IObjectComponent* TargetDeviationCompTemplate::createComponent()
{
    return new TargetDeviationComp(this);
}

void TargetDeviationCompTemplate::objectCreated(IObject*) {}
void TargetDeviationCompTemplate::objectDestroyed(IObject*) {}
void TargetDeviationCompTemplate::setTemplate(IObjectTemplate*) {}

void* TargetDeviationCompTemplate::queryImplementation(unsigned int classId)
{
    if (classId == CID_TargetDeviationCompTemplate || classId == CID_DeviationCompTemplate)
        return this;
    return 0;
}

TargetDeviationCompTemplate::~TargetDeviationCompTemplate()
{
}

bool TargetDeviationCompTemplate::makeScript(io::IStream* stream)
{
    std::ostringstream script;
    if (m_radius != 0.0f)
        script << "ObjectTemplate.deviation.radius " << m_radius << std::endl;

    const std::string text = script.str();
    return stream->write(text.c_str(), static_cast<unsigned int>(text.length())) == text.length();
}

} // namespace world
} // namespace hfe
} // namespace dice
