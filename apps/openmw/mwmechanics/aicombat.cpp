#include "aicombat.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include <components/misc/rng.hpp>
#include <components/settings/settings.hpp>
#include <components/misc/coordinateconverter.hpp>

#include <components/esm/aisequence.hpp>

#include <components/misc/mathutil.hpp>

#include <components/sceneutil/positionattitudetransform.hpp>
#include <components/detournavigator/navigator.hpp>

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include <components/openmw-mp/TimedLog.hpp>
#include "../mwmp/Main.hpp"
#include "../mwmp/Networking.hpp"
#include "../mwmp/ActorList.hpp"
#include "../mwmp/CellController.hpp"
#include "../mwmp/MechanicsHelper.hpp"
#include "../mwmp/PlayerList.hpp"
#include "../mwgui/windowmanagerimp.hpp"
/*
    End of tes3mp addition
*/

#include "../mwphysics/collisiontype.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/dialoguemanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"

#include "pathgrid.hpp"
#include "creaturestats.hpp"
#include "npcstats.hpp"
#include "steering.hpp"
#include "movement.hpp"
#include "character.hpp"
#include "aicombataction.hpp"
#include "actorutil.hpp"

namespace
{

    //chooses an attack depending on probability to avoid uniformity
    std::string chooseBestAttack(const ESM::Weapon* weapon);

    osg::Vec3f AimDirToMovingTarget(const MWWorld::Ptr& actor, const MWWorld::Ptr& target, const osg::Vec3f& vLastTargetPos,
        float duration, int weapType, float strength);
}

namespace MWMechanics
{
    AiCombat::AiCombat(const MWWorld::Ptr& actor)
    {
        mTargetActorId = actor.getClass().getCreatureStats(actor).getActorId();
    }

    AiCombat::AiCombat(const ESM::AiSequence::AiCombat *combat)
    {
        mTargetActorId = combat->mTargetActorId;
    }

    void AiCombat::init()
    {

    }

    /*
     * Current AiCombat movement states (as of 0.29.0), ignoring the details of the
     * attack states such as CombatMove, Strike and ReadyToAttack:
     *
     *    +----(within strike range)----->attack--(beyond strike range)-->follow
     *    |                                 | ^                            | |
     *    |                                 | |                            | |
     *  pursue<---(beyond follow range)-----+ +----(within strike range)---+ |
     *    ^                                                                  |
     *    |                                                                  |
     *    +-------------------------(beyond follow range)--------------------+
     *
     *
     * Below diagram is high level only, the code detail is a little different
     * (but including those detail will just complicate the diagram w/o adding much)
     *
     *    +----------(same)-------------->attack---------(same)---------->follow
     *    |                                 |^^                            |||
     *    |                                 |||                            |||
     *    |       +--(same)-----------------+|+----------(same)------------+||
     *    |       |                          |                              ||
     *    |       |                          | (in range)                   ||
     *    |   <---+         (too far)        |                              ||
     *  pursue<-------------------------[door open]<-----+                  ||
     *    ^^^                                            |                  ||
     *    |||                                            |                  ||
     *    ||+----------evade-----+                       |                  ||
     *    ||                     |    [closed door]      |                  ||
     *    |+----> maybe stuck, check --------------> back up, check door    ||
     *    |         ^   |   ^                          |   ^                ||
     *    |         |   |   |                          |   |                ||
     *    |         |   +---+                          +---+                ||
     *    |         +-------------------------------------------------------+|
     *    |                                                                  |
     *    +---------------------------(same)---------------------------------+
     *
     * FIXME:
     *
     * The new scheme is way too complicated, should really be implemented as a
     * proper state machine.
     *
     * TODO:
     *
     * Use the observer pattern to coordinate attacks, provide intelligence on
     * whether the target was hit, etc.
     */

    bool AiCombat::execute (const MWWorld::Ptr& actor, CharacterController& characterController, AiState& state, float duration)
    {
        AiCombatStorage& storage = state.get<AiCombatStorage>();
        storage.mMeleeCommitTimer = std::max(0.f, storage.mMeleeCommitTimer - duration);

        if (actor.getClass().getCreatureStats(actor).isDead())
        {
            clearTacticalMovement(actor, storage);
            return true;
        }

        MWWorld::Ptr target = MWBase::Environment::get().getWorld()->searchPtrViaActorId(mTargetActorId);
        if (target.isEmpty())
        {
            clearTacticalMovement(actor, storage);
            return false;
        }

        if (!target.getRefData().getCount() || !target.getRefData().isEnabled()
            || target.getClass().getCreatureStats(target).isDead())
        {
            clearTacticalMovement(actor, storage);
            return true;
        }

        if (actor == target)
        {
            clearTacticalMovement(actor, storage);
            return true;
        }

        if (updatePursuitLeash(actor, duration, storage))
        {
            clearTacticalMovement(actor, storage);
            storage.stopAttack();
            characterController.setAttackingOrSpell(false);
            return true;
        }

        if (!storage.isFleeing())
        {
            // Keep perception separate from pathing: when a sneaking player is
            // genuinely lost, AiCombat must stop consuming the player's real
            // coordinates and pursue only remembered/predicted positions.
            updateLOS(actor, target, duration, storage);
            if (updateStealthSearch(actor, target, duration, storage))
            {
                clearTacticalMovement(actor, storage);
                storage.stopAttack();
                characterController.setAttackingOrSpell(false);
                return true;
            }

            if (storage.mSearchingLastKnown)
            {
                clearTacticalMovement(actor, storage);
                storage.mFormationActive = false;
                storage.stopAttack();
                characterController.setAttackingOrSpell(false);
                actor.getClass().getCreatureStats(actor).setMovementFlag(CreatureStats::Flag_Run, true);

                const bool reached = pathTo(actor, storage.mSearchDestination, duration, 72.f);
                if (reached)
                {
                    ++storage.mSearchPointsVisited;
                    const int maxPoints = std::max(0,
                        Settings::Manager::getInt("combat stealth search points", "Game"));
                    if (storage.mSearchPointsVisited <= maxPoints)
                    {
                        const auto world = MWBase::Environment::get().getWorld();
                        const auto navigator = world->getNavigator();
                        const auto halfExtents = world->getPathfindingHalfExtents(actor);
                        const float radius = std::max(96.f,
                            Settings::Manager::getFloat("combat stealth search radius", "Game"));
                        const auto point = navigator->findRandomPointAroundCircle(
                            halfExtents, storage.mLastSeenTargetPos, radius, getNavigatorFlags(actor));
                        if (point.has_value())
                        {
                            storage.mSearchDestination = *point;
                            mPathFinder.clearPath();
                        }
                    }
                    else
                    {
                        auto& movement = actor.getClass().getMovementSettings(actor);
                        movement.mPosition[0] = 0.f;
                        movement.mPosition[1] = 0.f;
                    }
                }

                storage.mActionCooldown -= duration;
                return false;
            }

            bool meleePressureMovement = false;
            if (storage.mCurrentAction.get())
            {
                bool ranged = false;
                storage.mCurrentAction->getCombatRange(ranged);
                const float distToTarget = MWBase::Environment::get().getWorld()->getHitDistance(actor, target);
                const float meleeCommitRange = std::max(260.f, storage.mAttackRange * 1.45f);
                meleePressureMovement = Settings::Manager::getBool("combat melee pressure", "Game")
                    && !ranged && storage.mCurrentAction->isAttackingOrSpell() && storage.mLOS
                    && distToTarget <= meleeCommitRange;

                // Formation slots are an approach tool. Once a melee fighter has
                // committed to striking distance, continuing to chase a flank
                // slot is exactly what creates the unpleasant left/right pendulum.
                if (meleePressureMovement)
                {
                    // With direct geometric LOS, stale formation/custom flank
                    // destinations should never pull a committed melee fighter
                    // sideways away from the target.
                    if (storage.mFormationActive || (storage.mUseCustomDestination && storage.mGeometricLOS))
                        mPathFinder.clearPath();
                    storage.mFormationActive = false;
                    if (storage.mGeometricLOS)
                        storage.mUseCustomDestination = false;
                }
                else
                    updateFormationDestination(actor, target, duration, storage, ranged);

                const bool usesAlternateDestination = storage.mUseCustomDestination || storage.mFormationActive;
                const float targetReachedTolerance = storage.mLOS && !usesAlternateDestination
                    ? storage.mAttackRange : 0.0f;
                const osg::Vec3f destination = storage.mUseCustomDestination
                    ? storage.mCustomDestination
                    : (storage.mFormationActive ? storage.mFormationDestination
                                                : target.getRefData().getPosition().asVec3());
                const bool isTargetReached = pathTo(actor, destination, duration, targetReachedTolerance);
                if (isTargetReached && !usesAlternateDestination)
                    storage.mReadyToAttack = true;
                else if (isTargetReached && storage.mFormationActive)
                {
                    storage.mFormationActive = false;
                    mPathFinder.clearPath();
                }
            }

            storage.updateCombatMove(duration);
            updateTacticalMovement(actor, target, duration, storage, characterController);
            if (storage.mReadyToAttack || storage.hasTacticalMovement() || meleePressureMovement)
                updateActorsMovement(actor, duration, storage);
            storage.updateAttack(characterController);

            /*
                Start of tes3mp addition

                Record that this actor is updating an attack so that a packet will be sent about it
            */
            mwmp::Attack *localAttack = MechanicsHelper::getLocalAttack(actor);

            if (localAttack && localAttack->pressed != storage.mAttack)
            {
                MechanicsHelper::resetAttack(localAttack);
                localAttack->pressed = storage.mAttack;
                localAttack->shouldSend = true;
            }
            /*
                End of tes3mp addition
            */
        }
        else
        {
            clearTacticalMovement(actor, storage);
            storage.mFormationActive = false;
            updateFleeing(actor, target, duration, storage);
        }
        storage.mActionCooldown -= duration;

        if (storage.mReaction.update(duration) == Misc::TimerStatus::Waiting)
            return false;

        const bool finished = attack(actor, target, storage, characterController);
        if (finished)
            clearTacticalMovement(actor, storage);
        return finished;
    }

    bool AiCombat::attack(const MWWorld::Ptr& actor, const MWWorld::Ptr& target, AiCombatStorage& storage, CharacterController& characterController)
    {
        const MWWorld::CellStore*& currentCell = storage.mCell;
        bool cellChange = currentCell && (actor.getCell() != currentCell);
        if(!currentCell || cellChange)
        {
            currentCell = actor.getCell();
        }

        /*
            Start of tes3mp addition

            Because multiplayer doesn't pause the world during dialogue, disallow attacks on
            a player engaged in dialogue
        */
        if (target == MWBase::Environment::get().getWorld()->getPlayerPtr())
        {
            if (MWBase::Environment::get().getWindowManager()->containsMode(MWGui::GM_Dialogue))
            {
                storage.stopAttack();
                return false;
            }
        }
        /*
            End of tes3mp addition
        */

        bool forceFlee = false;
        if (!canFight(actor, target))
        {
            storage.stopAttack();
            characterController.setAttackingOrSpell(false);

            /*
                Start of tes3mp addition

                Record that this actor is stopping an attack so that a packet will be sent about it
            */
            mwmp::Attack *localAttack = MechanicsHelper::getLocalAttack(actor);

            if (localAttack && localAttack->pressed != false)
            {
                MechanicsHelper::resetAttack(localAttack);
                localAttack->pressed = false;
                localAttack->shouldSend = true;
            }
            /*
                End of tes3mp addition
            */

            storage.mActionCooldown = 0.f;
            // Continue combat if target is player or player follower/escorter and an attack has been attempted
            const std::list<MWWorld::Ptr>& playerFollowersAndEscorters = MWBase::Environment::get().getMechanicsManager()->getActorsSidingWith(MWMechanics::getPlayer());
            bool targetSidesWithPlayer = (std::find(playerFollowersAndEscorters.begin(), playerFollowersAndEscorters.end(), target) != playerFollowersAndEscorters.end());
            if ((target == MWMechanics::getPlayer() || targetSidesWithPlayer)
                && ((actor.getClass().getCreatureStats(actor).getHitAttemptActorId() == target.getClass().getCreatureStats(target).getActorId())
                || (target.getClass().getCreatureStats(target).getHitAttemptActorId() == actor.getClass().getCreatureStats(actor).getActorId())))
                forceFlee = true;
            else // Otherwise end combat
                return true;
        }

        const MWWorld::Class& actorClass = actor.getClass();
        actorClass.getCreatureStats(actor).setMovementFlag(CreatureStats::Flag_Run, true);

        float& actionCooldown = storage.mActionCooldown;
        std::shared_ptr<Action>& currentAction = storage.mCurrentAction;

        if (!forceFlee)
        {
            if (actionCooldown > 0)
                return false;

            const bool threatFlee = updateThreatFlee(actor, target, storage);
            if (threatFlee)
            {
                currentAction.reset(new ActionFlee());
                actionCooldown = currentAction->getActionCooldown();
            }
            else if (characterController.readyToPrepareAttack())
            {
                currentAction = prepareNextAction(actor, target);
                actionCooldown = currentAction->getActionCooldown();
            }
        }
        else
        {
            currentAction.reset(new ActionFlee());
            actionCooldown = currentAction->getActionCooldown();
        }

        if (!currentAction)
            return false;

        if (storage.isFleeing() != currentAction->isFleeing())
        {
            if (currentAction->isFleeing())
            {
                storage.startFleeing();
                MWBase::Environment::get().getDialogueManager()->say(actor, "flee");
                return false;
            }
            else
                storage.stopFleeing();
        }

        bool isRangedCombat = false;
        float &rangeAttack = storage.mAttackRange;

        rangeAttack = currentAction->getCombatRange(isRangedCombat);

        // Get weapon characteristics
        const ESM::Weapon* weapon = currentAction->getWeapon();

        ESM::Position pos = actor.getRefData().getPosition();
        const osg::Vec3f vActorPos(pos.asVec3());
        const osg::Vec3f vTargetPos(target.getRefData().getPosition().asVec3());

        float distToTarget = MWBase::Environment::get().getWorld()->getHitDistance(actor, target);

        storage.mReadyToAttack = (currentAction->isAttackingOrSpell() && distToTarget <= rangeAttack && storage.mLOS
            && !storage.suppressesAttack());

        // Once a melee fighter has genuinely closed the gap, briefly commit to
        // pressure/attack instead of re-rolling lateral tactics every reaction.
        // This is intentionally not used for ranged combat.
        if (!isRangedCombat && currentAction->isAttackingOrSpell() && storage.mLOS
            && Settings::Manager::getBool("combat melee pressure", "Game")
            && distToTarget <= std::max(280.f, rangeAttack * 1.30f))
        {
            storage.mMeleeCommitTimer = std::max(storage.mMeleeCommitTimer,
                Settings::Manager::getFloat("combat melee commit time", "Game"));
        }

        if (isRangedCombat)
        {
            // rotate actor taking into account target movement direction and projectile speed
            osg::Vec3f vAimDir = AimDirToMovingTarget(actor, target, storage.mLastTargetPos, AI_REACTION_TIME, (weapon ? weapon->mData.mType : 0), storage.mStrength);

            storage.mMovement.mRotation[0] = getXAngleToDir(vAimDir);
            storage.mMovement.mRotation[2] = getZAngleToDir(vAimDir);
        }
        else
        {
            osg::Vec3f vAimDir = MWBase::Environment::get().getWorld()->aimToTarget(actor, target, false);
            storage.mMovement.mRotation[0] = getXAngleToDir(vAimDir);
            storage.mMovement.mRotation[2] = getZAngleToDir((vTargetPos-vActorPos)); // using vAimDir results in spastic movements since the head is animated
        }

        storage.mLastTargetPos = vTargetPos;

        if (storage.mReadyToAttack)
        {
            storage.startCombatMove(isRangedCombat, distToTarget, rangeAttack, actor, target);
            // start new attack
            storage.startAttackIfReady(actor, characterController, weapon, isRangedCombat);
        }

        // If actor uses custom destination it has to try to rebuild path because environment can change
        // (door is opened between actor and target) or target position has changed and current custom destination
        // is not good enough to attack target.
        if (storage.mCurrentAction->isAttackingOrSpell()
            && ((!storage.mReadyToAttack && !mPathFinder.isPathConstructed())
                || (storage.mUseCustomDestination && (storage.mCustomDestination - vTargetPos).length() > rangeAttack)))
        {
            const MWBase::World* world = MWBase::Environment::get().getWorld();
            // Try to build path to the target.
            const auto halfExtents = world->getPathfindingHalfExtents(actor);
            const auto navigatorFlags = getNavigatorFlags(actor);
            const auto areaCosts = getAreaCosts(actor);
            const auto pathGridGraph = getPathGridGraph(actor.getCell());
            mPathFinder.buildPath(actor, vActorPos, vTargetPos, actor.getCell(), pathGridGraph, halfExtents, navigatorFlags, areaCosts);

            if (!mPathFinder.isPathConstructed())
            {
                // If there is no path, try to find a point on a line from the actor position to target projected
                // on navmesh to attack the target from there.
                const auto navigator = world->getNavigator();
                const auto hit = navigator->raycast(halfExtents, vActorPos, vTargetPos, navigatorFlags);

                if (hit.has_value() && (*hit - vTargetPos).length() <= rangeAttack)
                {
                    // If the point is close enough, try to find a path to that point.
                    mPathFinder.buildPath(actor, vActorPos, *hit, actor.getCell(), pathGridGraph, halfExtents, navigatorFlags, areaCosts);
                    if (mPathFinder.isPathConstructed())
                    {
                        // If path to that point is found use it as custom destination.
                        storage.mCustomDestination = *hit;
                        storage.mUseCustomDestination = true;
                    }
                }

                if (!mPathFinder.isPathConstructed())
                {
                    storage.mUseCustomDestination = false;

                    // A transient navmesh miss at arm's length must not turn an
                    // otherwise valid melee attack into panic/flee. With direct
                    // geometric LOS, keep pressure for a short distance and let
                    // the normal path builder retry on the following updates.
                    const bool closeVisibleMelee = Settings::Manager::getBool("combat melee pressure", "Game")
                        && !isRangedCombat && storage.mGeometricLOS
                        && distToTarget <= std::max(420.f, rangeAttack * 1.75f);
                    if (closeVisibleMelee)
                    {
                        mPathFinder.clearPath();
                        storage.mMovement.mPosition[0] = 0.f;
                        storage.mMovement.mPosition[1] = 0.9f;
                    }
                    else
                    {
                        storage.stopAttack();
                        characterController.setAttackingOrSpell(false);
                        currentAction.reset(new ActionFlee());
                        actionCooldown = currentAction->getActionCooldown();
                        storage.startFleeing();
                        MWBase::Environment::get().getDialogueManager()->say(actor, "flee");
                    }
                }
            }
            else
            {
                storage.mUseCustomDestination = false;
            }
        }

        return false;
    }

    bool AiCombat::updatePursuitLeash(const MWWorld::Ptr& actor, float duration, AiCombatStorage& storage)
    {
        if (mwmp::Main::isInitialized()
            && !mwmp::Main::get().getCellController()->isLocalActor(actor))
            return false;

        const MWWorld::CellStore* actorCell = actor.getCell();
        const osg::Vec3f actorPos = actor.getRefData().getPosition().asVec3();
        if (!storage.mCombatOriginSet)
        {
            storage.mCombatOrigin = actorPos;
            storage.mCombatOriginCell = actorCell;
            storage.mCombatOriginSet = true;
            storage.mLeashExceededTimer = 0.f;
            return false;
        }

        // Never rebase the leash origin after a door transition. Interior cell
        // coordinates are unrelated, so same-cell distance is meaningful only
        // in the original combat cell. Door breadcrumbs handle the return path.
        if (storage.mCombatOriginCell != actorCell)
        {
            storage.mLeashExceededTimer = 0.f;
            return false;
        }

        const float maxDistance = std::max(0.f,
            Settings::Manager::getFloat("combat pursuit max distance", "Game"));
        if (maxDistance <= 0.f)
            return false;

        const float distanceFromOrigin = distanceIgnoreZ(storage.mCombatOrigin, actorPos);
        if (distanceFromOrigin > maxDistance)
            storage.mLeashExceededTimer += duration;
        else
            storage.mLeashExceededTimer = std::max(0.f, storage.mLeashExceededTimer - duration * 2.f);

        return storage.mLeashExceededTimer >= 1.5f;
    }

    void AiCombat::clearTacticalMovement(const MWWorld::Ptr& actor, AiCombatStorage& storage)
    {
        const bool controlsActor = !mwmp::Main::isInitialized()
            || mwmp::Main::get().getCellController()->isLocalActor(actor);
        if (controlsActor)
        {
            MWMechanics::CreatureStats& stats = actor.getClass().getCreatureStats(actor);
            stats.setMovementFlag(CreatureStats::Flag_ForceMoveJump, false);
            stats.setMovementFlag(CreatureStats::Flag_ForceSneak, false);
        }

        storage.mTacticalState = AiCombatStorage::Tactical_None;
        storage.mTacticalTimer = 0.f;
        storage.mJumpTimer = 0.f;
        storage.mSneakTimer = 0.f;
        storage.mMovement.mPosition[0] = 0.f;
        storage.mMovement.mPosition[1] = 0.f;
    }

    void AiCombat::updateTacticalMovement(const MWWorld::Ptr& actor, const MWWorld::Ptr& target, float duration,
        AiCombatStorage& storage, CharacterController& characterController)
    {
        // Only the client that currently owns this actor may make tactical AI
        // movement decisions. Other clients receive the resulting movement/attack
        // state through ArenaMP synchronization.
        if (mwmp::Main::isInitialized()
            && !mwmp::Main::get().getCellController()->isLocalActor(actor))
        {
            storage.mTacticalState = AiCombatStorage::Tactical_None;
            storage.mTacticalTimer = 0.f;
            return;
        }

        if (!Settings::Manager::getBool("tactical combat", "Game") || !storage.mCurrentAction)
        {
            clearTacticalMovement(actor, storage);
            return;
        }

        MWMechanics::CreatureStats& stats = actor.getClass().getCreatureStats(actor);
        const osg::Vec3f actorPos = actor.getRefData().getPosition().asVec3();
        const float distToTarget = MWBase::Environment::get().getWorld()->getHitDistance(actor, target);
        const bool bipedal = actor.getClass().isBipedal(actor);
        bool ranged = false;
        storage.mCurrentAction->getCombatRange(ranged);
        const bool aggressiveMelee = Settings::Manager::getBool("combat melee pressure", "Game")
            && !ranged && storage.mCurrentAction->isAttackingOrSpell();
        const float meleeCommitRange = std::max(260.f, storage.mAttackRange * 1.45f);
        const bool meleePressureRange = aggressiveMelee && storage.mLOS && distToTarget <= meleeCommitRange;

        storage.mTacticalCooldown = std::max(0.f, storage.mTacticalCooldown - duration);
        storage.mTacticalDecisionTimer = std::max(0.f, storage.mTacticalDecisionTimer - duration);

        if (storage.mJumpTimer > 0.f)
        {
            storage.mJumpTimer -= duration;
            if (storage.mJumpTimer <= 0.f)
                stats.setMovementFlag(CreatureStats::Flag_ForceMoveJump, false);
        }
        if (storage.mSneakTimer > 0.f)
        {
            storage.mSneakTimer -= duration;
            if (storage.mSneakTimer <= 0.f)
                stats.setMovementFlag(CreatureStats::Flag_ForceSneak, false);
        }

        // Detect a blocked pursuit without replacing StartCombat by AiTravel.
        storage.mStuckCheckTimer -= duration;
        if (storage.mStuckCheckTimer <= 0.f)
        {
            if (storage.mHasLastActorPos && distToTarget > 250.f
                && distanceIgnoreZ(storage.mLastActorPos, actorPos) < 40.f)
                storage.mStuckDuration += 1.5f;
            else
                storage.mStuckDuration = 0.f;

            storage.mLastActorPos = actorPos;
            storage.mHasLastActorPos = true;
            storage.mStuckCheckTimer = 1.5f;
        }

        if (storage.mStuckDuration >= 3.f && bipedal)
        {
            storage.mStuckDuration = 0.f;
            storage.mTacticalState = AiCombatStorage::Tactical_Unstuck;
            storage.mTacticalTimer = 0.45f;
            storage.mTacticalCooldown = 1.2f;
            storage.stopAttack();
            storage.mMovement.mPosition[0] = Misc::Rng::rollProbability() < 0.5 ? -1.f : 1.f;
            storage.mMovement.mPosition[1] = 0.65f;
            stats.setMovementFlag(CreatureStats::Flag_ForceMoveJump, true);
            storage.mJumpTimer = 0.32f;
            characterController.setAttackingOrSpell(false);
            mPathFinder.clearPath();
        }

        if (storage.mTacticalState != AiCombatStorage::Tactical_None)
        {
            storage.mTacticalTimer -= duration;
            if (storage.mTacticalTimer <= 0.f)
            {
                storage.mTacticalState = AiCombatStorage::Tactical_None;
                storage.mMovement.mPosition[0] = 0.f;
                storage.mMovement.mPosition[1] = 0.f;
            }
        }

        const float hpMax = stats.getHealth().getModified();
        const float hpRatio = hpMax > 0.f ? stats.getHealth().getCurrent() / hpMax : 1.f;

        // Do not let close-range tactical decoration interrupt an actual melee
        // commitment. Real stuck recovery is kept, and a critically wounded
        // fighter may still retreat after the current swing has finished.
        if (meleePressureRange && storage.mTacticalState != AiCombatStorage::Tactical_Unstuck)
        {
            const bool criticallyRetreating = storage.mTacticalState == AiCombatStorage::Tactical_Retreat
                && hpRatio < 0.20f && !storage.mAttack && storage.mMeleeCommitTimer <= 0.f;
            if (!criticallyRetreating && storage.mTacticalState != AiCombatStorage::Tactical_None)
                clearTacticalMovement(actor, storage);
        }

        if (storage.mTacticalState == AiCombatStorage::Tactical_None
            && storage.mTacticalCooldown <= 0.f && storage.mTacticalDecisionTimer <= 0.f)
        {
            storage.mTacticalDecisionTimer = 0.45f;
            const float roll = Misc::Rng::rollClosedProbability();

            const bool wantsRetreat = ranged
                ? (hpRatio < 0.35f && distToTarget < 500.f && roll < 0.45f)
                : (hpRatio < 0.20f && distToTarget < 450.f && !storage.mAttack
                    && storage.mMeleeCommitTimer <= 0.f && roll < 0.25f);
            if (wantsRetreat)
            {
                storage.mTacticalState = AiCombatStorage::Tactical_Retreat;
                storage.mTacticalTimer = 0.55f;
                storage.mTacticalCooldown = ranged ? 1.8f : 2.4f;
            }
            else if (meleePressureRange)
            {
                // Close melee is intentionally boring in the good sense: face
                // the target, press, swing, recover, swing again.
            }
            else if (bipedal && distToTarget < std::max(300.f, storage.mAttackRange * 1.35f)
                && roll < (ranged ? 0.08f : 0.05f))
            {
                storage.mTacticalState = AiCombatStorage::Tactical_JumpDodge;
                storage.mTacticalTimer = 0.38f;
                storage.mTacticalCooldown = 2.5f;
                stats.setMovementFlag(CreatureStats::Flag_ForceMoveJump, true);
                storage.mJumpTimer = 0.28f;
                storage.stopAttack();
                characterController.setAttackingOrSpell(false);
            }
            else if (distToTarget < std::max(380.f, storage.mAttackRange * 1.6f)
                && roll < (ranged ? 0.35f : 0.12f))
            {
                const bool left = Misc::Rng::rollProbability() < 0.5;
                storage.mTacticalState = left
                    ? AiCombatStorage::Tactical_CircleLeft : AiCombatStorage::Tactical_CircleRight;
                storage.mTacticalTimer = 0.45f + 0.35f * Misc::Rng::rollClosedProbability();
                storage.mTacticalCooldown = 1.2f + 1.5f * Misc::Rng::rollClosedProbability();
            }
            else if (!ranged && distToTarget < 650.f && roll < 0.20f)
            {
                const bool left = Misc::Rng::rollProbability() < 0.5;
                storage.mTacticalState = left
                    ? AiCombatStorage::Tactical_StrafeLeft : AiCombatStorage::Tactical_StrafeRight;
                storage.mTacticalTimer = 0.25f + 0.35f * Misc::Rng::rollClosedProbability();
                storage.mTacticalCooldown = 1.0f + 1.3f * Misc::Rng::rollClosedProbability();
            }
            else if (bipedal && !ranged && distToTarget > 500.f && distToTarget < 1000.f && roll > 0.92f)
            {
                storage.mTacticalState = AiCombatStorage::Tactical_SneakApproach;
                storage.mTacticalTimer = 1.5f + Misc::Rng::rollClosedProbability();
                storage.mTacticalCooldown = 4.f;
                stats.setMovementFlag(CreatureStats::Flag_ForceSneak, true);
                storage.mSneakTimer = storage.mTacticalTimer;
            }
        }

        switch (storage.mTacticalState)
        {
            case AiCombatStorage::Tactical_StrafeLeft:
                storage.mMovement.mPosition[0] = -1.f;
                storage.mMovement.mPosition[1] = 0.15f;
                break;
            case AiCombatStorage::Tactical_StrafeRight:
                storage.mMovement.mPosition[0] = 1.f;
                storage.mMovement.mPosition[1] = 0.15f;
                break;
            case AiCombatStorage::Tactical_CircleLeft:
                storage.mMovement.mPosition[0] = -1.f;
                storage.mMovement.mPosition[1] = ranged ? -0.15f : 0.35f;
                break;
            case AiCombatStorage::Tactical_CircleRight:
                storage.mMovement.mPosition[0] = 1.f;
                storage.mMovement.mPosition[1] = ranged ? -0.15f : 0.35f;
                break;
            case AiCombatStorage::Tactical_Retreat:
                storage.stopAttack();
                storage.mMovement.mPosition[0] = Misc::Rng::rollProbability() < 0.5 ? -0.35f : 0.35f;
                storage.mMovement.mPosition[1] = -1.f;
                characterController.setAttackingOrSpell(false);
                break;
            case AiCombatStorage::Tactical_JumpDodge:
            case AiCombatStorage::Tactical_Unstuck:
                if (storage.mMovement.mPosition[0] == 0.f)
                    storage.mMovement.mPosition[0] = Misc::Rng::rollProbability() < 0.5 ? -1.f : 1.f;
                storage.mMovement.mPosition[1] = storage.mTacticalState == AiCombatStorage::Tactical_Unstuck ? 0.65f : -0.2f;
                break;
            case AiCombatStorage::Tactical_SneakApproach:
            case AiCombatStorage::Tactical_None:
                break;
        }

        if (meleePressureRange && storage.mTacticalState == AiCombatStorage::Tactical_None)
        {
            storage.mMovement.mPosition[0] = 0.f;
            const float closeStop = std::max(32.f, storage.mAttackRange * 0.35f);
            if (distToTarget > closeStop)
                storage.mMovement.mPosition[1] = storage.mMeleeCommitTimer > 0.f ? 0.85f : 1.f;
            else
                storage.mMovement.mPosition[1] = storage.mMeleeCommitTimer > 0.f ? 0.18f : 0.f;
        }
    }

    void MWMechanics::AiCombat::updateLOS(const MWWorld::Ptr& actor, const MWWorld::Ptr& target, float duration, MWMechanics::AiCombatStorage& storage)
    {
        static const float LOS_UPDATE_DURATION = 0.4f;
        if (storage.mUpdateLOSTimer <= 0.f)
        {
            MWBase::World* world = MWBase::Environment::get().getWorld();
            MWBase::MechanicsManager* mechanics = MWBase::Environment::get().getMechanicsManager();
            storage.mGeometricLOS = world->getLOS(actor, target);

            bool detected = storage.mGeometricLOS;
            if (detected && target == MWMechanics::getPlayer() && mechanics->isSneaking(target))
                detected = mechanics->awarenessCheck(target, actor);

            storage.mLOS = detected;
            storage.mUpdateLOSTimer = LOS_UPDATE_DURATION;

            if (detected)
            {
                const osg::Vec3f targetPos = target.getRefData().getPosition().asVec3();
                if (storage.mHasLastSeenTarget)
                {
                    osg::Vec3f velocity = (targetPos - storage.mLastSeenTargetPos) / LOS_UPDATE_DURATION;
                    const float maxRememberedSpeed = 650.f;
                    if (velocity.length2() > maxRememberedSpeed * maxRememberedSpeed)
                    {
                        velocity.normalize();
                        velocity *= maxRememberedSpeed;
                    }
                    storage.mLastSeenTargetVelocity = velocity;
                }
                storage.mLastSeenTargetPos = targetPos;
                storage.mHasLastSeenTarget = true;
                storage.mLostSightTimer = 0.f;
                storage.mSearchElapsed = 0.f;
                storage.mSearchingLastKnown = false;
                storage.mSearchPointsVisited = 0;
            }
        }
        else
            storage.mUpdateLOSTimer -= duration;
    }

    bool AiCombat::updateStealthSearch(const MWWorld::Ptr& actor, const MWWorld::Ptr& target, float duration,
        AiCombatStorage& storage)
    {
        const bool targetIsPlayer = target == MWMechanics::getPlayer()
            || mwmp::PlayerList::isDedicatedPlayer(target);
        if (!Settings::Manager::getBool("combat stealth search", "Game")
            || !targetIsPlayer)
        {
            storage.mSearchingLastKnown = false;
            storage.mLostSightTimer = 0.f;
            storage.mSearchElapsed = 0.f;
            return false;
        }

        MWBase::MechanicsManager* mechanics = MWBase::Environment::get().getMechanicsManager();
        if (storage.mLOS)
            return false;

        if (!mechanics->isSneaking(target) || !storage.mHasLastSeenTarget)
        {
            storage.mSearchingLastKnown = false;
            storage.mLostSightTimer = 0.f;
            storage.mSearchElapsed = 0.f;
            return false;
        }

        storage.mLostSightTimer += duration;
        const float searchDelay = std::max(0.f,
            Settings::Manager::getFloat("combat stealth search delay", "Game"));
        if (storage.mLostSightTimer < searchDelay)
            return false;

        storage.mSearchElapsed += duration;
        if (!storage.mSearchingLastKnown)
        {
            const float predictionSeconds = std::max(0.f,
                Settings::Manager::getFloat("combat stealth prediction time", "Game"));
            osg::Vec3f predicted = storage.mLastSeenTargetPos
                + storage.mLastSeenTargetVelocity * predictionSeconds;

            // Never extrapolate an arbitrarily long sprint or a teleport. A short
            // predicted corridor is enough to create a believable search without
            // leaking the player's actual hidden position into pathfinding.
            osg::Vec3f predictionDelta = predicted - storage.mLastSeenTargetPos;
            const float maxPredictionDistance = 420.f;
            if (predictionDelta.length2() > maxPredictionDistance * maxPredictionDistance)
            {
                predictionDelta.normalize();
                predicted = storage.mLastSeenTargetPos + predictionDelta * maxPredictionDistance;
            }

            MWBase::World* world = MWBase::Environment::get().getWorld();
            const osg::Vec3f actorPos = actor.getRefData().getPosition().asVec3();
            const auto hit = world->getNavigator()->raycast(world->getPathfindingHalfExtents(actor),
                actorPos, predicted, getNavigatorFlags(actor));
            storage.mSearchDestination = hit.has_value() ? *hit : storage.mLastSeenTargetPos;
            storage.mSearchingLastKnown = true;
            storage.mSearchPointsVisited = 0;
            storage.mFormationActive = false;
            storage.mUseCustomDestination = false;
            mPathFinder.clearPath();
        }

        const float giveUpTime = std::max(searchDelay + 0.5f,
            Settings::Manager::getFloat("combat stealth search give up time", "Game"));

        // A target may be hidden by Sneak mechanics while still physically in
        // front of the NPC. Do not declare the search over until geometric LOS
        // is also gone; this prevents absurd disengagement while staring at the
        // player in an unobstructed corridor.
        return storage.mSearchElapsed >= giveUpTime && !storage.mGeometricLOS;
    }

    void AiCombat::updateFormationDestination(const MWWorld::Ptr& actor, const MWWorld::Ptr& target, float duration,
        AiCombatStorage& storage, bool ranged)
    {
        storage.mFormationUpdateTimer = std::max(0.f, storage.mFormationUpdateTimer - duration);
        if (!Settings::Manager::getBool("combat formation slots", "Game") || !storage.mLOS
            || storage.mSearchingLastKnown || storage.mUseCustomDestination)
        {
            storage.mFormationActive = false;
            return;
        }

        if (storage.mFormationUpdateTimer > 0.f)
            return;
        storage.mFormationUpdateTimer = 0.45f;

        std::list<MWWorld::Ptr> attackers
            = MWBase::Environment::get().getMechanicsManager()->getActorsFighting(target);
        std::vector<MWWorld::Ptr> sameCell;
        sameCell.reserve(attackers.size());
        for (const MWWorld::Ptr& attacker : attackers)
        {
            if (attacker.isEmpty() || attacker.getCell() != actor.getCell()
                || attacker.getClass().getCreatureStats(attacker).isDead())
                continue;
            sameCell.push_back(attacker);
        }

        const int maxSlots = std::max(2, Settings::Manager::getInt("combat formation max actors", "Game"));
        if (sameCell.size() < 2)
        {
            storage.mFormationActive = false;
            return;
        }

        std::sort(sameCell.begin(), sameCell.end(), [](const MWWorld::Ptr& left, const MWWorld::Ptr& right) {
            return left.getClass().getCreatureStats(left).getActorId()
                < right.getClass().getCreatureStats(right).getActorId();
        });

        if (sameCell.size() > static_cast<std::size_t>(maxSlots))
            sameCell.resize(static_cast<std::size_t>(maxSlots));
        const auto it = std::find(sameCell.begin(), sameCell.end(), actor);
        if (it == sameCell.end())
        {
            storage.mFormationActive = false;
            return;
        }

        const std::size_t index = static_cast<std::size_t>(std::distance(sameCell.begin(), it));
        const float count = static_cast<float>(sameCell.size());
        const float radius = ranged
            ? std::max(340.f, std::min(720.f, storage.mAttackRange * 0.55f))
            : std::max(82.f, std::min(170.f, storage.mAttackRange * 0.70f));
        const float phase = static_cast<float>(target.getClass().getCreatureStats(target).getActorId() & 7)
            * (osg::PI / 16.f);
        const float angle = phase + osg::PI * 2.f * static_cast<float>(index) / count;
        const osg::Vec3f targetPos = target.getRefData().getPosition().asVec3();
        const osg::Vec3f candidate(targetPos.x() + std::sin(angle) * radius,
            targetPos.y() + std::cos(angle) * radius, targetPos.z());

        MWBase::World* world = MWBase::Environment::get().getWorld();
        const auto hit = world->getNavigator()->raycast(world->getPathfindingHalfExtents(actor),
            targetPos, candidate, getNavigatorFlags(actor));
        if (!hit.has_value() || distanceIgnoreZ(*hit, targetPos) < 48.f)
        {
            storage.mFormationActive = false;
            return;
        }

        if (distanceIgnoreZ(actor.getRefData().getPosition().asVec3(), *hit) <= 72.f)
        {
            storage.mFormationActive = false;
            return;
        }

        storage.mFormationDestination = *hit;
        storage.mFormationActive = true;
    }

    bool AiCombat::updateThreatFlee(const MWWorld::Ptr& actor, const MWWorld::Ptr& target, AiCombatStorage& storage)
    {
        if (!Settings::Manager::getBool("combat threat flee", "Game")
            || !actor.getClass().isNpc() || actor.getClass().isClass(actor, "Guard"))
        {
            storage.mThreatFlee = false;
            return false;
        }

        storage.mThreatUpdateTimer -= AI_REACTION_TIME;
        if (storage.mThreatUpdateTimer > 0.f)
            return storage.mThreatFlee;
        storage.mThreatUpdateTimer = 0.9f;

        const CreatureStats& actorStats = actor.getClass().getCreatureStats(actor);
        const CreatureStats& targetStats = target.getClass().getCreatureStats(target);
        const float actorHealthMax = std::max(1.f, actorStats.getHealth().getModified());
        const float actorHpRatio = std::max(0.f, actorStats.getHealth().getCurrent() / actorHealthMax);

        auto power = [](const CreatureStats& stats) {
            return std::max(1.f, static_cast<float>(stats.getLevel()) * 10.f
                + stats.getHealth().getModified() * 0.34f
                + stats.getMagicka().getModified() * 0.09f);
        };

        float actorPower = power(actorStats);
        float targetPower = power(targetStats);
        MWBase::MechanicsManager* mechanics = MWBase::Environment::get().getMechanicsManager();
        int allies = 0;
        for (const MWWorld::Ptr& ally : mechanics->getActorsFighting(target))
            if (!ally.isEmpty() && ally.getCell() == actor.getCell())
                ++allies;
        int enemies = 0;
        for (const MWWorld::Ptr& enemy : mechanics->getActorsFighting(actor))
            if (!enemy.isEmpty() && enemy.getCell() == actor.getCell())
                ++enemies;
        actorPower *= 1.f + std::min(3, std::max(0, allies - 1)) * 0.18f;
        targetPower *= 1.f + std::min(3, std::max(0, enemies - 1)) * 0.18f;

        const float ratio = actorPower / std::max(1.f, targetPower);
        const float fight = std::max(0, std::min(100,
            actorStats.getAiSetting(CreatureStats::AI_Fight).getModified())) / 100.f;
        const float flee = std::max(0, std::min(100,
            actorStats.getAiSetting(CreatureStats::AI_Flee).getModified())) / 100.f;
        float threshold = std::max(0.15f,
            Settings::Manager::getFloat("combat threat flee ratio", "Game"));
        threshold += (1.f - actorHpRatio) * 0.28f + flee * 0.18f - fight * 0.16f;

        storage.mThreatFlee = ratio < threshold
            && (actorHpRatio < 0.72f || ratio < threshold * 0.72f);
        return storage.mThreatFlee;
    }

    void MWMechanics::AiCombat::updateFleeing(const MWWorld::Ptr& actor, const MWWorld::Ptr& target, float duration, MWMechanics::AiCombatStorage& storage)
    {
        static const float BLIND_RUN_DURATION = 1.0f;

        updateLOS(actor, target, duration, storage);

        AiCombatStorage::FleeState& state = storage.mFleeState;
        switch (state)
        {
            case AiCombatStorage::FleeState_None:
                return;

            case AiCombatStorage::FleeState_Idle:
                {
                    const bool targetIsPlayer = target == MWMechanics::getPlayer()
                        || mwmp::PlayerList::isDedicatedPlayer(target);
                    if (!storage.mFleeAskedGuard && actor.getClass().isNpc()
                        && targetIsPlayer
                        && Settings::Manager::getBool("combat flee seek guard", "Game")
                        && MWBase::Environment::get().getMechanicsManager()->isCombatInitiator(target, actor))
                    {
                        const float guardRadius = std::max(256.f,
                            Settings::Manager::getFloat("combat flee seek guard radius", "Game"));
                        std::vector<MWWorld::Ptr> nearby;
                        MWBase::Environment::get().getMechanicsManager()->getActorsInRange(
                            actor.getRefData().getPosition().asVec3(), guardRadius, nearby);
                        MWWorld::Ptr bestGuard;
                        float bestDistance2 = std::numeric_limits<float>::max();
                        for (const MWWorld::Ptr& candidate : nearby)
                        {
                            if (candidate.isEmpty() || candidate == actor || candidate == target
                                || candidate.getCell() != actor.getCell() || !candidate.getClass().isNpc()
                                || !candidate.getClass().isClass(candidate, "Guard")
                                || candidate.getClass().getCreatureStats(candidate).isDead())
                                continue;
                            const float dist2 = (candidate.getRefData().getPosition().asVec3()
                                - actor.getRefData().getPosition().asVec3()).length2();
                            if (dist2 < bestDistance2)
                            {
                                bestDistance2 = dist2;
                                bestGuard = candidate;
                            }
                        }

                        if (!bestGuard.isEmpty())
                        {
                            storage.mFleeGuardActorId
                                = bestGuard.getClass().getCreatureStats(bestGuard).getActorId();
                            state = AiCombatStorage::FleeState_RunToGuard;
                            mPathFinder.clearPath();
                            break;
                        }
                    }

                    float triggerDist = getMaxAttackDistance(target);

                    if (storage.mLOS &&
                            (triggerDist >= 1000 || getDistanceMinusHalfExtents(actor, target) <= triggerDist))
                    {
                        const ESM::Pathgrid* pathgrid =
                                MWBase::Environment::get().getWorld()->getStore().get<ESM::Pathgrid>().search(*storage.mCell->getCell());

                        bool runFallback = true;

                        if (pathgrid != nullptr && !pathgrid->mPoints.empty() && !actor.getClass().isPureWaterCreature(actor))
                        {
                            ESM::Pathgrid::PointList points;
                            Misc::CoordinateConverter coords(storage.mCell->getCell());

                            osg::Vec3f localPos = actor.getRefData().getPosition().asVec3();
                            coords.toLocal(localPos);

                            int closestPointIndex = PathFinder::getClosestPoint(pathgrid, localPos);
                            for (int i = 0; i < static_cast<int>(pathgrid->mPoints.size()); i++)
                            {
                                if (i != closestPointIndex && getPathGridGraph(storage.mCell).isPointConnected(closestPointIndex, i))
                                {
                                    points.push_back(pathgrid->mPoints[static_cast<size_t>(i)]);
                                }
                            }

                            if (!points.empty())
                            {
                                ESM::Pathgrid::Point dest = points[Misc::Rng::rollDice(points.size())];
                                coords.toWorld(dest);

                                state = AiCombatStorage::FleeState_RunToDestination;
                                storage.mFleeDest = ESM::Pathgrid::Point(dest.mX, dest.mY, dest.mZ);

                                runFallback = false;
                            }
                        }

                        if (runFallback)
                        {
                            state = AiCombatStorage::FleeState_RunBlindly;
                            storage.mFleeBlindRunTimer = 0.0f;
                        }
                    }
                }
                break;

            case AiCombatStorage::FleeState_RunBlindly:
                {
                    // timer to prevent twitchy movement that can be observed in vanilla MW
                    if (storage.mFleeBlindRunTimer < BLIND_RUN_DURATION)
                    {
                        storage.mFleeBlindRunTimer += duration;

                        storage.mMovement.mRotation[0] = -actor.getRefData().getPosition().rot[0];
                        storage.mMovement.mRotation[2] = osg::PI + getZAngleToDir(target.getRefData().getPosition().asVec3()-actor.getRefData().getPosition().asVec3());
                        storage.mMovement.mPosition[1] = 1;
                        updateActorsMovement(actor, duration, storage);
                    }
                    else
                        state = AiCombatStorage::FleeState_Idle;
                }
                break;

            case AiCombatStorage::FleeState_RunToDestination:
                {
                    static const float fFleeDistance = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>().find("fFleeDistance")->mValue.getFloat();

                    float dist = (actor.getRefData().getPosition().asVec3() - target.getRefData().getPosition().asVec3()).length();
                    if ((dist > fFleeDistance && !storage.mLOS)
                            || pathTo(actor, PathFinder::makeOsgVec3(storage.mFleeDest), duration))
                    {
                        state = AiCombatStorage::FleeState_Idle;
                    }
                }
                break;

            case AiCombatStorage::FleeState_RunToGuard:
                {
                    MWWorld::Ptr guard = MWBase::Environment::get().getWorld()
                        ->searchPtrViaActorId(storage.mFleeGuardActorId);
                    if (guard.isEmpty() || guard.getCell() != actor.getCell()
                        || guard.getClass().getCreatureStats(guard).isDead())
                    {
                        storage.mFleeGuardActorId = -1;
                        state = AiCombatStorage::FleeState_Idle;
                        break;
                    }

                    const float guardDistance = distanceIgnoreZ(
                        actor.getRefData().getPosition().asVec3(), guard.getRefData().getPosition().asVec3());
                    if (guardDistance <= 180.f
                        || pathTo(actor, guard.getRefData().getPosition().asVec3(), duration, 140.f))
                    {
                        // Only victims for whom the player is the recorded initiator
                        // take this branch, so hostile bandits cannot abuse city guards
                        // after attacking the player themselves.
                        MWBase::Environment::get().getMechanicsManager()->startCombat(guard, target);
                        storage.mFleeAskedGuard = true;
                        storage.mFleeGuardActorId = -1;
                        state = AiCombatStorage::FleeState_RunBlindly;
                        storage.mFleeBlindRunTimer = 0.f;
                        mPathFinder.clearPath();
                    }
                }
                break;
        };
    }

    void AiCombat::updateActorsMovement(const MWWorld::Ptr& actor, float duration, AiCombatStorage& storage)
    {
        // apply combat movement
        float deltaAngle = storage.mMovement.mRotation[2] - actor.getRefData().getPosition().rot[2];
        osg::Vec2f movement = Misc::rotateVec2f(
            osg::Vec2f(storage.mMovement.mPosition[0], storage.mMovement.mPosition[1]), -deltaAngle);

        MWMechanics::Movement& actorMovementSettings = actor.getClass().getMovementSettings(actor);
        actorMovementSettings.mPosition[0] = movement.x();
        actorMovementSettings.mPosition[1] = movement.y();
        actorMovementSettings.mPosition[2] = storage.mMovement.mPosition[2];

        rotateActorOnAxis(actor, 2, actorMovementSettings, storage);
        rotateActorOnAxis(actor, 0, actorMovementSettings, storage);
    }

    void AiCombat::rotateActorOnAxis(const MWWorld::Ptr& actor, int axis, 
        MWMechanics::Movement& actorMovementSettings, AiCombatStorage& storage)
    {
        actorMovementSettings.mRotation[axis] = 0;
        bool isRangedCombat = false;
        storage.mCurrentAction->getCombatRange(isRangedCombat);
        float eps = isRangedCombat ? osg::DegreesToRadians(0.5) : osg::DegreesToRadians(3.f);
        float targetAngleRadians = storage.mMovement.mRotation[axis];
        smoothTurn(actor, targetAngleRadians, axis, eps);
    }

    MWWorld::Ptr AiCombat::getTarget() const
    {
        return MWBase::Environment::get().getWorld()->searchPtrViaActorId(mTargetActorId);
    }

    void AiCombat::writeState(ESM::AiSequence::AiSequence &sequence) const
    {
        std::unique_ptr<ESM::AiSequence::AiCombat> combat(new ESM::AiSequence::AiCombat());
        combat->mTargetActorId = mTargetActorId;

        ESM::AiSequence::AiPackageContainer package;
        package.mType = ESM::AiSequence::Ai_Combat;
        package.mPackage = combat.release();
        sequence.mPackages.push_back(package);
    }

    void AiCombatStorage::startCombatMove(bool isDistantCombat, float distToTarget, float rangeAttack, const MWWorld::Ptr& actor, const MWWorld::Ptr& target)
    {
        // get the range of the target's weapon
        MWWorld::Ptr targetWeapon = MWWorld::Ptr();
        const MWWorld::Class& targetClass = target.getClass();

        if (targetClass.hasInventoryStore(target))
        {
            int weapType = ESM::Weapon::None;
            MWWorld::ContainerStoreIterator weaponSlot = MWMechanics::getActiveWeapon(target, &weapType);
            if (weapType > ESM::Weapon::None)
                targetWeapon = *weaponSlot;
        }

        bool targetUsesRanged = false;
        float rangeAttackOfTarget = ActionWeapon(targetWeapon).getCombatRange(targetUsesRanged);

        if (!isDistantCombat && Settings::Manager::getBool("combat melee pressure", "Game")
            && mMeleeCommitTimer > 0.f)
        {
            // Commit the body to the swing: face the target and keep a modest
            // amount of forward pressure instead of random side-stepping.
            mMovement.mPosition[0] = 0.f;
            mMovement.mPosition[1] = distToTarget > rangeAttack * 0.68f ? 0.72f : 0.f;
            mTimerCombatMove = 0.22f;
            mCombatMove = true;
            return;
        }
        
        if (mMovement.mPosition[0] || mMovement.mPosition[1])
        {
            mTimerCombatMove = 0.1f + 0.1f * Misc::Rng::rollClosedProbability();
            mCombatMove = true;
        }
        else if (isDistantCombat)
        {
            // Backing up behaviour
            // Actor backs up slightly further away than opponent's weapon range
            // (in vanilla - only as far as oponent's weapon range),
            // or not at all if opponent is using a ranged weapon

            if (targetUsesRanged || distToTarget > rangeAttackOfTarget*1.5) // Don't back up if the target is wielding ranged weapon
                return;

            // actor should not back up into water
            if (MWBase::Environment::get().getWorld()->isUnderwater(MWWorld::ConstPtr(actor), 0.5f))
                return;

            int mask = MWPhysics::CollisionType_World | MWPhysics::CollisionType_HeightMap | MWPhysics::CollisionType_Door;

            // Actor can not back up if there is no free space behind
            // Currently we take the 35% of actor's height from the ground as vector height.
            // This approach allows us to detect small obstacles (e.g. crates) and curved walls.
            osg::Vec3f halfExtents = MWBase::Environment::get().getWorld()->getHalfExtents(actor);
            osg::Vec3f pos = actor.getRefData().getPosition().asVec3();
            osg::Vec3f source = pos + osg::Vec3f(0, 0, 0.75f * halfExtents.z());
            osg::Vec3f fallbackDirection = actor.getRefData().getBaseNode()->getAttitude() * osg::Vec3f(0,-1,0);
            osg::Vec3f destination = source + fallbackDirection * (halfExtents.y() + 16);

            bool isObstacleDetected = MWBase::Environment::get().getWorld()->castRay(source.x(), source.y(), source.z(), destination.x(), destination.y(), destination.z(), mask);
            if (isObstacleDetected)
                return;

            // Check if there is nothing behind - probably actor is near cliff.
            // A current approach: cast ray 1.5-yard ray down in 1.5 yard behind actor from 35% of actor's height.
            // If we did not hit anything, there is a cliff behind actor.
            source = pos + osg::Vec3f(0, 0, 0.75f * halfExtents.z()) + fallbackDirection * (halfExtents.y() + 96);
            destination = source - osg::Vec3f(0, 0, 0.75f * halfExtents.z() + 96);
            bool isCliffDetected = !MWBase::Environment::get().getWorld()->castRay(source.x(), source.y(), source.z(), destination.x(), destination.y(), destination.z(), mask);
            if (isCliffDetected)
                return;

            mMovement.mPosition[1] = -1;
        }
        // dodge movements (for NPCs and bipedal creatures)
        // Note: do not use for ranged combat yet since in couple with back up behaviour can move actor out of cliff
        else if (actor.getClass().isBipedal(actor))
        {
            float moveDuration = 0;
            float angleToTarget = Misc::normalizeAngle(mMovement.mRotation[2] - actor.getRefData().getPosition().rot[2]);
            // Apply a big side step if enemy tries to get around and come from behind.
            // Otherwise apply a random side step (kind of dodging) with some probability
            // if actor is within range of target's weapon.
            if (std::abs(angleToTarget) > osg::PI / 4)
                moveDuration = 0.2f;
            else if (distToTarget <= rangeAttackOfTarget
                && Misc::Rng::rollClosedProbability() < (Settings::Manager::getBool("combat melee pressure", "Game") ? 0.10f : 0.25f))
                moveDuration = 0.1f + 0.1f * Misc::Rng::rollClosedProbability();
            if (moveDuration > 0)
            {
                mMovement.mPosition[0] = Misc::Rng::rollProbability() < 0.5 ? 1.0f : -1.0f; // to the left/right
                mTimerCombatMove = moveDuration;
                mCombatMove = true;
            }
        }
    }

    void AiCombatStorage::updateCombatMove(float duration)
    {
        if (mCombatMove)
        {
            mTimerCombatMove -= duration;
            if (mTimerCombatMove <= 0)
            {
                stopCombatMove();
            }
        }
    }

    void AiCombatStorage::stopCombatMove()
    {
        mTimerCombatMove = 0;
        mMovement.mPosition[1] = mMovement.mPosition[0] = 0;
        mCombatMove = false;
    }

    void AiCombatStorage::startAttackIfReady(const MWWorld::Ptr& actor, CharacterController& characterController, 
        const ESM::Weapon* weapon, bool distantCombat)
    {
        if (mReadyToAttack && characterController.readyToStartAttack())
        {
            if (mAttackCooldown <= 0)
            {
                mAttack = true; // attack starts just now
                characterController.setAttackingOrSpell(true);

                if (!distantCombat)
                    characterController.setAIAttackType(chooseBestAttack(weapon));

                /*
                    Start of tes3mp addition

                    Record that this actor is starting an attack so that a packet will be sent about it
                */
                mwmp::Attack *localAttack = MechanicsHelper::getLocalAttack(actor);

                if (localAttack && localAttack->pressed != true)
                {
                    MechanicsHelper::resetAttack(localAttack);
                    localAttack->type = distantCombat ? mwmp::Attack::RANGED : mwmp::Attack::MELEE;
                    localAttack->attackAnimation = characterController.getAttackType();
                    localAttack->pressed = true;

                    mwmp::ActorList *actorList = mwmp::Main::get().getNetworking()->getActorList();
                    actorList->reset();
                    actorList->cell = *actor.getCell()->getCell();
                    actorList->addAttackActor(actor, *localAttack);
                    actorList->sendAttackActors();
                }
                /*
                    End of tes3mp addition
                */
                const bool meleePressure = !distantCombat
                    && Settings::Manager::getBool("combat melee pressure", "Game");
                if (meleePressure)
                {
                    float minStrength = std::max(0.f, std::min(1.f,
                        Settings::Manager::getFloat("combat melee charge min", "Game")));
                    float maxStrength = std::max(0.f, std::min(1.f,
                        Settings::Manager::getFloat("combat melee charge max", "Game")));
                    if (maxStrength < minStrength)
                        std::swap(maxStrength, minStrength);
                    mStrength = minStrength + (maxStrength - minStrength) * Misc::Rng::rollClosedProbability();
                    mMeleeCommitTimer = std::max(0.15f,
                        Settings::Manager::getFloat("combat melee commit time", "Game"));
                }
                else
                    mStrength = Misc::Rng::rollClosedProbability();

                const MWWorld::ESMStore &store = MWBase::Environment::get().getWorld()->getStore();

                float baseDelay = store.get<ESM::GameSetting>().find("fCombatDelayCreature")->mValue.getFloat();
                if (actor.getClass().isNpc())
                {
                    baseDelay = store.get<ESM::GameSetting>().find("fCombatDelayNPC")->mValue.getFloat();
                }

                // Say a provoking combat phrase
                const int iVoiceAttackOdds = store.get<ESM::GameSetting>().find("iVoiceAttackOdds")->mValue.getInteger();
                if (Misc::Rng::roll0to99() < iVoiceAttackOdds)
                {
                    MWBase::Environment::get().getDialogueManager()->say(actor, "attack");
                }
                float attackCooldown = std::min(baseDelay + 0.01 * Misc::Rng::roll0to99(), baseDelay + 0.9);
                if (meleePressure)
                {
                    const float cooldownScale = std::max(0.2f, std::min(1.5f,
                        Settings::Manager::getFloat("combat melee cooldown scale", "Game")));
                    attackCooldown *= cooldownScale;
                }
                mAttackCooldown = std::max(0.05f, attackCooldown);
            }
            else
                mAttackCooldown -= AI_REACTION_TIME;
        }
    }

    void AiCombatStorage::updateAttack(CharacterController& characterController)
    {
        if (mAttack)
        {
            const bool charged = characterController.getAttackStrength() >= mStrength;
            const bool animationReset = characterController.readyToPrepareAttack();
            if (charged || (animationReset && mMeleeCommitTimer <= 0.f))
                mAttack = false;
        }
        characterController.setAttackingOrSpell(mAttack);
    }

    void AiCombatStorage::stopAttack()
    {
        mMovement.mPosition[0] = 0;
        mMovement.mPosition[1] = 0;
        mMovement.mPosition[2] = 0;
        mReadyToAttack = false;
        mAttack = false;
        mMeleeCommitTimer = 0.f;
    }

    void AiCombatStorage::startFleeing()
    {
        stopFleeing();
        mFleeState = FleeState_Idle;
        mFleeGuardActorId = -1;
        mFleeAskedGuard = false;
    }

    void AiCombatStorage::stopFleeing()
    {
        mMovement.mPosition[0] = 0;
        mMovement.mPosition[1] = 0;
        mMovement.mPosition[2] = 0;
        mFleeState = FleeState_None;
        mFleeDest = ESM::Pathgrid::Point(0, 0, 0);
        mFleeGuardActorId = -1;
    }

    bool AiCombatStorage::isFleeing()
    {
        return mFleeState != FleeState_None;
    }

    bool AiCombatStorage::hasTacticalMovement() const
    {
        return mTacticalState != Tactical_None && mTacticalState != Tactical_SneakApproach;
    }

    bool AiCombatStorage::suppressesAttack() const
    {
        return mTacticalState == Tactical_Retreat || mTacticalState == Tactical_JumpDodge
            || mTacticalState == Tactical_Unstuck;
    }
}


namespace
{

std::string chooseBestAttack(const ESM::Weapon* weapon)
{
    std::string attackType;

    if (weapon != nullptr)
    {
        //the more damage attackType deals the more probability it has
        int slash = (weapon->mData.mSlash[0] + weapon->mData.mSlash[1])/2;
        int chop = (weapon->mData.mChop[0] + weapon->mData.mChop[1])/2;
        int thrust = (weapon->mData.mThrust[0] + weapon->mData.mThrust[1])/2;

        float roll = Misc::Rng::rollClosedProbability() * (slash + chop + thrust);
        if(roll <= slash)
            attackType = "slash";
        else if(roll <= (slash + thrust))
            attackType = "thrust";
        else
            attackType = "chop";
    }
    else
        MWMechanics::CharacterController::setAttackTypeRandomly(attackType);

    return attackType;
}

osg::Vec3f AimDirToMovingTarget(const MWWorld::Ptr& actor, const MWWorld::Ptr& target, const osg::Vec3f& vLastTargetPos,
    float duration, int weapType, float strength)
{
    float projSpeed;
    const MWWorld::Store<ESM::GameSetting>& gmst = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>();

    // get projectile speed (depending on weapon type)
    if (MWMechanics::getWeaponType(weapType)->mWeaponClass == ESM::WeaponType::Thrown)
    {
        static float fThrownWeaponMinSpeed = gmst.find("fThrownWeaponMinSpeed")->mValue.getFloat();
        static float fThrownWeaponMaxSpeed = gmst.find("fThrownWeaponMaxSpeed")->mValue.getFloat();

        projSpeed = fThrownWeaponMinSpeed + (fThrownWeaponMaxSpeed - fThrownWeaponMinSpeed) * strength;
    }
    else if (weapType != 0)
    {
        static float fProjectileMinSpeed = gmst.find("fProjectileMinSpeed")->mValue.getFloat();
        static float fProjectileMaxSpeed = gmst.find("fProjectileMaxSpeed")->mValue.getFloat();

        projSpeed = fProjectileMinSpeed + (fProjectileMaxSpeed - fProjectileMinSpeed) * strength;
    }
    else // weapType is 0 ==> it's a target spell projectile
    {
        projSpeed = gmst.find("fTargetSpellMaxSpeed")->mValue.getFloat();
    }

    // idea: perpendicular to dir to target speed components of target move vector and projectile vector should be the same

    osg::Vec3f vTargetPos = target.getRefData().getPosition().asVec3();
    osg::Vec3f vDirToTarget = MWBase::Environment::get().getWorld()->aimToTarget(actor, target, true);
    float distToTarget = vDirToTarget.length();

    osg::Vec3f vTargetMoveDir = vTargetPos - vLastTargetPos;
    vTargetMoveDir /= duration; // |vTargetMoveDir| is target real speed in units/sec now

    osg::Vec3f vPerpToDir = vDirToTarget ^ osg::Vec3f(0,0,1); // cross product

    vPerpToDir.normalize();
    osg::Vec3f vDirToTargetNormalized = vDirToTarget;
    vDirToTargetNormalized.normalize();

    // dot product
    float velPerp = vTargetMoveDir * vPerpToDir;
    float velDir = vTargetMoveDir * vDirToTargetNormalized;

    // time to collision between target and projectile
    float t_collision;

    float projVelDirSquared = projSpeed * projSpeed - velPerp * velPerp;
    if (projVelDirSquared > 0)
    {
        osg::Vec3f vTargetMoveDirNormalized = vTargetMoveDir;
        vTargetMoveDirNormalized.normalize();

        float projDistDiff = vDirToTarget * vTargetMoveDirNormalized; // dot product
        projDistDiff = std::sqrt(distToTarget * distToTarget - projDistDiff * projDistDiff);

        t_collision = projDistDiff / (std::sqrt(projVelDirSquared) - velDir);
    }
    else
        t_collision = 0; // speed of projectile is not enough to reach moving target

    return vDirToTarget + vTargetMoveDir * t_collision;
}

}
