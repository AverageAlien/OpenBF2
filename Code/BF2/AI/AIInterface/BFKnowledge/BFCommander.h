// Exposes commander actions to the AI.
// Preserve virtual declaration order. [INFERRED] Return types require validation where implementations remain incomplete.
// [INFERRED] Source placement.

#ifndef AI_AIINTERFACE_BFKNOWLEDGE_BFCOMMANDER_H
#define AI_AIINTERFACE_BFKNOWLEDGE_BFCOMMANDER_H

#include <list>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "AIAdapterTypes.h"

namespace dice {
namespace hfe {

class IBase;
enum SquadOrderType : int;     // Game/GameLogic/SquadManager/ISquadInfo.h

namespace ai {

class IBFCommander
{
public:
    IBFCommander();
    virtual ~IBFCommander();

    virtual int applyAsCommander(IAIPlayer* player) = 0;
    virtual int retireAsCommander(IAIPlayer* player) = 0;
    virtual int giveAttackOrder(const Vec3& position, int a, int b, IAIObject* target) = 0;
    virtual int giveMoveOrder(const Vec3& position, int a, int b, IAIObject* target) = 0;
    virtual int giveDefendOrder(const Vec3& position, int a, int b, IAIObject* target) = 0;
    virtual void sendAttackMessage(int a, int b) = 0;
    virtual void sendDefendMessage(int a, int b) = 0;
    virtual int fireArtillery(int a, int b, Vec2 position) = 0;
    virtual void sendUav(int a, int b, Vec2 position) = 0;
    virtual void dropSupply(int a, int b, Vec2 position) = 0;
    virtual void dropVehicle(int a, int b, Vec2 position) = 0;
    virtual void denyArtilleryRequest(int a, int b) = 0;
    virtual bool gameHasArtillery(int team) = 0;
    virtual bool anyArtilleryPiecesLeft(int team) = 0;
    virtual void ArtilleryUnavailable(int a, int b) = 0;
    virtual void denySupplyRequest(int a, int b) = 0;
    virtual void denyVehicleDropRequest(int a, int b) = 0;
    virtual void denyUavRequest(int a, int b) = 0;
    // [CHOSEN, not recovered] OpenBF2 extension, appended slots. Retail's AI route (fireArtillery)
    // posts a request that ArtilleryEvent::executeServer accepts only from the team's commander,
    // so an AI commander's strike never fired. fireArtilleryAsAI fires the team's artillery at the
    // XZ position directly when the team has no human commander and the artillery is ready;
    // isArtilleryReady says whether the team has guns left and its reload is over.
    virtual bool fireArtilleryAsAI(int team, Vec2 position) = 0;
    virtual bool isArtilleryReady(int team) = 0;
};

class BFCommander : public IBFCommander
{
public:
    BFCommander();
    virtual ~BFCommander();

    virtual int applyAsCommander(IAIPlayer* player);
    virtual int retireAsCommander(IAIPlayer* player);
    virtual int giveAttackOrder(const Vec3& position, int a, int b, IAIObject* target);
    virtual int giveMoveOrder(const Vec3& position, int a, int b, IAIObject* target);
    virtual int giveDefendOrder(const Vec3& position, int a, int b, IAIObject* target);
    virtual void sendAttackMessage(int a, int b);
    virtual void sendDefendMessage(int a, int b);
    virtual int fireArtillery(int a, int b, Vec2 position);
    virtual void sendUav(int a, int b, Vec2 position);
    virtual void dropSupply(int a, int b, Vec2 position);
    virtual void dropVehicle(int a, int b, Vec2 position);
    virtual void denyArtilleryRequest(int a, int b);
    virtual bool gameHasArtillery(int team);
    virtual bool anyArtilleryPiecesLeft(int team);
    virtual void ArtilleryUnavailable(int a, int b);
    virtual void denySupplyRequest(int a, int b);
    virtual void denyVehicleDropRequest(int a, int b);
    virtual void denyUavRequest(int a, int b);
    virtual bool fireArtilleryAsAI(int team, Vec2 position);
    virtual bool isArtilleryReady(int team);

private:
    // bf2 ELF 0x840F740: the shared body of the three give*Order methods.
    void giveOrder(const Vec3& position, int squadId, int team, IAIObject* target, SquadOrderType type);
};

} // namespace ai
} // namespace hfe
} // namespace dice

#endif // AI_AIINTERFACE_BFKNOWLEDGE_BFCOMMANDER_H
