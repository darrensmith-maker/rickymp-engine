#include "actionteleport.hpp"

#include <algorithm>
#include <list>
#include <utility>
#include <vector>

#include <components/misc/rng.hpp>

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include <components/openmw-mp/TimedLog.hpp>
#include <components/settings/settings.hpp>
#include "../mwbase/windowmanager.hpp"
#include "../mwmp/Main.hpp"
#include "../mwmp/Networking.hpp"
#include "../mwmp/ActorList.hpp"
#include "../mwmp/CellController.hpp"
#include "../mwmp/MechanicsHelper.hpp"
/*
    End of tes3mp addition
*/

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwbase/mechanicsmanager.hpp"

#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/aisequence.hpp"
#include "../mwmechanics/aiwander.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/cellstore.hpp"

#include "player.hpp"

namespace
{
    struct QueuedTeleport
    {
        MWWorld::Ptr mActor;
        std::string mCellName;
        ESM::Position mPosition;
        float mDelay = 0.f;
        int mRequiredCombatTargetId = -1;
        bool mReturnHome = false;
    };

    std::vector<QueuedTeleport> sQueuedTeleports;
}

namespace MWWorld
{
    ActionTeleport::ActionTeleport (const std::string& cellName,
        const ESM::Position& position, bool teleportFollowers)
    : Action (true), mCellName (cellName), mPosition (position), mTeleportFollowers(teleportFollowers)
    {
    }

    void ActionTeleport::executeImp (const Ptr& actor)
    {
        if (mTeleportFollowers)
        {
            // Find any NPCs that are following the actor and teleport them with him
            std::set<MWWorld::Ptr> followers;
            const bool includeHostilePursuers = Settings::Manager::getBool("combat pursuit through doors", "Game");
            getFollowers(actor, followers, includeHostilePursuers);

            std::size_t delayedPursuerIndex = 0;
            for (std::set<MWWorld::Ptr>::iterator it = followers.begin(); it != followers.end(); ++it)
            {
                MWMechanics::AiSequence& sequence = it->getClass().getCreatureStats(*it).getAiSequence();
                const bool hostilePursuer = sequence.isInCombat(actor);
                const bool delayedPursuit = hostilePursuer
                    && Settings::Manager::getBool("combat pursuit delayed door transition", "Game");

                // Door history is required for ReturnHome even when the optional
                // visual delay is disabled. Record it for every admitted hostile
                // pursuer before choosing immediate vs delayed cell transfer.
                if (hostilePursuer)
                {
                    MWBase::World* world = MWBase::Environment::get().getWorld();
                    const ESM::CellId fromCellId = it->getCell()->getCell()->getCellId();
                    const std::string fromCellName = it->getCell()->isExterior()
                        ? std::string() : it->getCell()->getCell()->mName;
                    const ESM::Position fromPosition = it->getRefData().getPosition();

                    MWWorld::CellStore* destinationCell = nullptr;
                    if (mCellName.empty())
                    {
                        int cellX = 0;
                        int cellY = 0;
                        world->positionToIndex(mPosition.pos[0], mPosition.pos[1], cellX, cellY);
                        destinationCell = world->getExterior(cellX, cellY);
                    }
                    else
                        destinationCell = world->getInterior(mCellName);

                    if (destinationCell)
                    {
                        sequence.recordDoorTransition(fromCellId, fromCellName, fromPosition,
                            destinationCell->getCell()->getCellId(), mPosition);
                    }
                }

                if (!delayedPursuit)
                {
                    teleport(*it, actor);
                    continue;
                }

                const float minDelay = std::max(0.f,
                    Settings::Manager::getFloat("combat pursuit door delay min", "Game"));
                const float maxDelay = std::max(minDelay,
                    Settings::Manager::getFloat("combat pursuit door delay max", "Game"));
                const float randomDelay = minDelay
                    + (maxDelay - minDelay) * Misc::Rng::rollClosedProbability();
                const float stagger = static_cast<float>(delayedPursuerIndex++) * 0.12f;
                const int targetActorId = actor.getClass().getCreatureStats(actor).getActorId();
                queueDelayedTeleport(*it, mCellName, mPosition, randomDelay + stagger, targetActorId);
            }
        }

        teleport(actor);
    }

    void ActionTeleport::teleport(const Ptr& actor, const Ptr& teleportTarget, bool returnHome)
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        actor.getClass().getCreatureStats(actor).land(actor == world->getPlayerPtr());
        if(actor == world->getPlayerPtr())
        {
            world->getPlayer().setTeleported(true);
            if (mCellName.empty())
                world->changeToExteriorCell (mPosition, true);
            else
                world->changeToInteriorCell (mCellName, mPosition, true);
        }
        else
        {
            /*
                Start of tes3mp addition

                Track the original cell of this actor so we can use it when sending a packet
            */
            ESM::Cell originalCell = *actor.getCell()->getCell();
            /*
                End of tes3mp addition
            */

            /*
                Start of tes3mp change (minor)

                If this is a DedicatedActor, get their new cell and override their stored cell with it
                so their cell change is approved in World::moveObject()
            */
            MWWorld::CellStore* newCellStore = nullptr;
            mwmp::CellController* cellController = mwmp::Main::get().getCellController();
            const bool isCombatPursuer = !teleportTarget.isEmpty()
                && actor.getClass().getCreatureStats(actor).getAiSequence().isInCombat(teleportTarget);

            if (mCellName.empty())
            {
                int cellX;
                int cellY;
                world->positionToIndex(mPosition.pos[0], mPosition.pos[1], cellX, cellY);

                newCellStore = world->getExterior(cellX, cellY);
                if (cellController->isDedicatedActor(actor))
                    cellController->getDedicatedActor(actor)->cell = *newCellStore->getCell();

                world->moveObject(actor, newCellStore,
                    mPosition.pos[0], mPosition.pos[1], mPosition.pos[2]);
            }
            else
            {
                newCellStore = world->getInterior(mCellName);
                if (cellController->isDedicatedActor(actor))
                    cellController->getDedicatedActor(actor)->cell = *newCellStore->getCell();

                world->moveObject(actor, newCellStore, mPosition.pos[0], mPosition.pos[1], mPosition.pos[2]);
            }
            /*
                Start of tes3mp change (minor)
            */

            /*
                Start of tes3mp addition

                Send ActorCellChange packets when an actor follows us across cells, regardless of
                whether we're the cell authority or not; the server can decide if it wants to comply
                with them

                Afterwards, send an ActorAI packet about this actor being our follower, to ensure
                they remain our follower even if the destination cell has another player as its
                cell authority
            */
            mwmp::BaseActor baseActor;
            baseActor.refNum = actor.getCellRef().getRefNum().mIndex;
            baseActor.mpNum = actor.getCellRef().getMpNum();
            baseActor.cell = *newCellStore->getCell();
            baseActor.position = actor.getRefData().getPosition();
            // Pursuit/follower transitions deliberately use TES3MP's follower
            // bypass. Return-home is different: it is an AI-owned move and must
            // only be accepted from the current cell authority.
            baseActor.isFollowerCellChange = !returnHome;

            mwmp::ActorList *actorList = mwmp::Main::get().getNetworking()->getActorList();
            actorList->reset();
            actorList->cell = originalCell;

            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_ACTOR_CELL_CHANGE about %s %i-%i to server",
                actor.getCellRef().getRefId().c_str(), baseActor.refNum, baseActor.mpNum);

            LOG_APPEND(TimedLog::LOG_INFO, "- Moved from %s to %s", actorList->cell.getDescription().c_str(),
                baseActor.cell.getDescription().c_str());

            actorList->addCellChangeActor(baseActor);
            actorList->sendCellChangeActors();

            // Send ActorAI to bring all players in the new cell up to speed.
            // Hostile pursuers keep AiCombat and ordinary followers keep AiFollow.
            // A return-home transition must never fall through to FOLLOW(player):
            // restore the suspended Wander package instead (or CANCEL for actors
            // that had no persistent AI package before combat).
            actorList->cell = baseActor.cell;
            if (returnHome)
            {
                const MWMechanics::AiSequence& sequence
                    = actor.getClass().getCreatureStats(actor).getAiSequence();
                const MWMechanics::AiWander* wander = nullptr;
                for (auto it = sequence.begin(); it != sequence.end(); ++it)
                {
                    if ((*it)->getTypeId() == MWMechanics::AiPackageTypeId::Wander)
                    {
                        wander = static_cast<const MWMechanics::AiWander*>(it->get());
                        break;
                    }
                }

                baseActor.hasAiTarget = false;
                if (wander)
                {
                    baseActor.aiAction = mwmp::BaseActorList::WANDER;
                    baseActor.aiDistance = static_cast<unsigned int>(wander->getDistance());
                    baseActor.aiDuration = static_cast<unsigned int>(wander->getDuration());
                    baseActor.aiShouldRepeat = wander->getRepeat();
                }
                else
                    baseActor.aiAction = mwmp::BaseActorList::CANCEL;
            }
            else
            {
                baseActor.aiAction = isCombatPursuer
                    ? mwmp::BaseActorList::COMBAT : mwmp::BaseActorList::FOLLOW;
                const MWWorld::Ptr aiTarget = !teleportTarget.isEmpty()
                    ? teleportTarget : world->getPlayerPtr();
                baseActor.aiTarget = MechanicsHelper::getTarget(aiTarget);
            }
            actorList->addAiActor(baseActor);
            actorList->sendAiActors();
            /*
                End of tes3mp addition
            */
        }
    }

    void ActionTeleport::queueDelayedTeleport(const MWWorld::Ptr& actor, const std::string& cellName,
        const ESM::Position& position, float delay, int requiredCombatTargetId, bool returnHome)
    {
        if (actor.isEmpty() || !actor.getClass().isActor())
            return;

        // Delayed actor movement must remain owned by the same MP authority that queued it.
        // If authority has already moved elsewhere, the new controller will receive normal AI/cell state.
        if (mwmp::Main::isInitialized())
        {
            mwmp::CellController* controller = mwmp::Main::get().getCellController();
            if (!controller || !controller->isLocalActor(actor))
                return;
        }

        QueuedTeleport queued;
        queued.mActor = actor;
        queued.mCellName = cellName;
        queued.mPosition = position;
        queued.mDelay = std::max(0.f, delay);
        queued.mRequiredCombatTargetId = requiredCombatTargetId;
        queued.mReturnHome = returnHome;
        sQueuedTeleports.push_back(std::move(queued));
    }

    void ActionTeleport::updateDelayedTeleports(float duration)
    {
        if (sQueuedTeleports.empty())
            return;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        for (auto it = sQueuedTeleports.begin(); it != sQueuedTeleports.end();)
        {
            it->mDelay -= duration;
            if (it->mDelay > 0.f)
            {
                ++it;
                continue;
            }

            MWWorld::Ptr actor = it->mActor;
            if (actor.isEmpty() || !actor.getRefData().getCount() || !actor.getRefData().isEnabled()
                || !actor.getClass().isActor() || actor.getClass().getCreatureStats(actor).isDead())
            {
                it = sQueuedTeleports.erase(it);
                continue;
            }

            // Important MP distinction:
            // - forward pursuit through a player-used door was authorized while the
            //   pursuer was still a LocalActor in the origin cell; after the player
            //   changes cells TES3MP immediately uninitializes that LocalActor record.
            //   Requiring isLocalActor() again here therefore cancels every delayed
            //   chase 0.7-1.4 seconds later. Forward pursuit intentionally uses the
            //   established follower-cell-change bypass, so let the queued transfer
            //   finish even if the origin cell has already been unloaded.
            // - ReturnHome is an autonomous AI move, not a follower transfer, and must
            //   still be owned by the current cell authority at execution time.
            if (it->mReturnHome && mwmp::Main::isInitialized())
            {
                mwmp::CellController* controller = mwmp::Main::get().getCellController();
                if (!controller || !controller->isLocalActor(actor))
                {
                    it = sQueuedTeleports.erase(it);
                    continue;
                }
            }

            MWWorld::Ptr combatTarget;
            if (it->mRequiredCombatTargetId >= 0)
            {
                combatTarget = world->searchPtrViaActorId(it->mRequiredCombatTargetId);
                if (combatTarget.isEmpty()
                    || !actor.getClass().getCreatureStats(actor).getAiSequence().isInCombat(combatTarget))
                {
                    it = sQueuedTeleports.erase(it);
                    continue;
                }
            }

            ActionTeleport action(it->mCellName, it->mPosition, false);
            action.teleport(actor, combatTarget, it->mReturnHome);
            it = sQueuedTeleports.erase(it);
        }
    }

    void ActionTeleport::clearDelayedTeleports()
    {
        sQueuedTeleports.clear();
    }

    void ActionTeleport::getFollowers(const MWWorld::Ptr& actor, std::set<MWWorld::Ptr>& out, bool includeHostiles) {
        std::set<MWWorld::Ptr> followers;
        MWBase::MechanicsManager* mechanics = MWBase::Environment::get().getMechanicsManager();
        mechanics->getActorsFollowing(actor, followers);

        // Combat pursuers are deliberately collected separately instead of pretending that
        // AiCombat is an AiFollow package. This keeps ally/follower semantics unchanged.
        if (includeHostiles)
        {
            const std::list<MWWorld::Ptr> pursuers = mechanics->getActorsFighting(actor);
            followers.insert(pursuers.begin(), pursuers.end());
        }

        std::size_t hostilePursuerCount = 0;
        const int maxHostilePursuers = std::max(0,
            Settings::Manager::getInt("combat pursuit max actors", "Game"));
        const float guaranteedDistance = std::max(0.f,
            Settings::Manager::getFloat("combat pursuit guaranteed distance", "Game"));
        const float maximumDoorDistance = std::max(guaranteedDistance,
            Settings::Manager::getFloat("combat pursuit door max distance", "Game"));
        const float minimumChance = std::max(0.f, std::min(1.f,
            Settings::Manager::getFloat("combat pursuit minimum chance", "Game")));

        for(std::set<MWWorld::Ptr>::iterator it = followers.begin();it != followers.end();++it)
        {
            MWWorld::Ptr follower = *it;

            std::string script = follower.getClass().getScript(follower);

            const bool isHostilePursuer = follower.getClass().getCreatureStats(follower).getAiSequence().isInCombat(actor);
            if (!includeHostiles && isHostilePursuer)
                continue;
            if (isHostilePursuer && mwmp::Main::isInitialized()
                && !mwmp::Main::get().getCellController()->isLocalActor(follower))
                continue;

            if (!script.empty() && follower.getRefData().getLocals().getIntVar(script, "stayoutside") == 1)
                continue;

            const float distance = (follower.getRefData().getPosition().asVec3()
                - actor.getRefData().getPosition().asVec3()).length();

            if (isHostilePursuer)
            {
                // Only actors with a preserved return-home package may cross teleport doors.
                // This avoids stranding scripted Travel/Escort actors in the player's destination cell.
                const MWMechanics::AiSequence& sequence = follower.getClass().getCreatureStats(follower).getAiSequence();
                const int maxDoorTransitions = std::max(1,
                    Settings::Manager::getInt("combat pursuit max door transitions", "Game"));
                const bool humanoid = follower.getClass().isNpc() || follower.getClass().isBipedal(follower);
                if (!sequence.hasPackage(MWMechanics::AiPackageTypeId::InternalTravel)
                    || sequence.getReturnHomeDoorTransitionCount() >= static_cast<std::size_t>(maxDoorTransitions)
                    || !humanoid || maxHostilePursuers == 0
                    || hostilePursuerCount >= static_cast<std::size_t>(maxHostilePursuers))
                    continue;
                if (distance > maximumDoorDistance)
                    continue;

                float pursuitChance = 1.f;
                if (distance > guaranteedDistance && maximumDoorDistance > guaranteedDistance)
                {
                    const float normalizedDistance = std::min(1.f,
                        (distance - guaranteedDistance) / (maximumDoorDistance - guaranteedDistance));
                    pursuitChance = 1.f - normalizedDistance * (1.f - minimumChance);
                }

                if (Misc::Rng::rollClosedProbability() > pursuitChance)
                    continue;

                ++hostilePursuerCount;
            }
            else if (distance > 800.f)
            {
                // Keep the original follower teleport radius.
                continue;
            }

            out.emplace(follower);
        }
    }
}
