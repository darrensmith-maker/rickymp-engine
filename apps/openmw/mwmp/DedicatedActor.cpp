#include <components/openmw-mp/TimedLog.hpp>
#include <cmath>

#include "../mwbase/environment.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/windowmanager.hpp"

#include "../mwdialogue/dialoguemanagerimp.hpp"

#include "../mwmechanics/aiactivate.hpp"
#include "../mwmechanics/aicombat.hpp"
#include "../mwmechanics/aiescort.hpp"
#include "../mwmechanics/aifollow.hpp"
#include "../mwmechanics/aitravel.hpp"
#include "../mwmechanics/aiwander.hpp"

#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/mechanicsmanagerimp.hpp"
#include "../mwmechanics/movement.hpp"

#include "../mwrender/animation.hpp"

#include "../mwworld/action.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/worldimp.hpp"

#include "DedicatedActor.hpp"
#include "Main.hpp"
#include "CellController.hpp"
#include "MechanicsHelper.hpp"
#include "InteractionAnimationSync.hpp"

namespace
{
    bool interactionAnimationIsBlocked(const MWWorld::Ptr& ptr)
    {
        if (ptr.isEmpty())
            return true;

        const MWMechanics::CreatureStats& stats = ptr.getClass().getCreatureStats(ptr);
        const MWMechanics::Movement& movement = ptr.getClass().getMovementSettings(ptr);
        const bool moving = std::abs(movement.mPosition[0]) > 0.05f
            || std::abs(movement.mPosition[1]) > 0.05f
            || std::abs(movement.mPosition[2]) > 0.05f;

        return moving || stats.isDead() || stats.getKnockedDown()
            || stats.getAiSequence().isInCombat()
            || stats.getDrawState() != MWMechanics::DrawState_Nothing
            || MWBase::Environment::get().getWorld()->isSwimming(ptr);
    }
}

using namespace mwmp;

DedicatedActor::DedicatedActor()
{
    drawState = MWMechanics::DrawState_::DrawState_Nothing;
    movementFlags = 0;
    animation.groupname = "";
    sound = "";

    hasPositionData = false;
    hasStatsDynamicData = false;
    hasReceivedInitialEquipment = false;
    hasChangedCell = true;

    attack.pressed = false;
    cast.pressed = false;

    mInteractionAnimationActive = false;
}

DedicatedActor::~DedicatedActor()
{
    cancelInteractionAnimation();
}

void DedicatedActor::update(float dt)
{
    // Only move and set anim flags if the framerate isn't too low
    if (dt < 0.1)
    {
        move(dt);
        setAnimFlags();
    }

    updateInteractionAnimation();
    setStatsDynamic();
}

void DedicatedActor::setCell(MWWorld::CellStore *cellStore)
{
    MWBase::World *world = MWBase::Environment::get().getWorld();

    ptr = world->moveObject(ptr, cellStore, position.pos[0], position.pos[1], position.pos[2]);
    setMovementSettings();

    hasChangedCell = true;
}

void DedicatedActor::move(float dt)
{
    ESM::Position refPos = ptr.getRefData().getPosition();
    const ESM::Position previousRefPos = refPos;
    MWBase::World *world = MWBase::Environment::get().getWorld();
    const int maxInterpolationDistance = 40;

    // Apply interpolation only if the position hasn't changed too much from last time
    bool shouldInterpolate = abs(position.pos[0] - refPos.pos[0]) < maxInterpolationDistance && abs(position.pos[1] - refPos.pos[1]) < maxInterpolationDistance && abs(position.pos[2] - refPos.pos[2]) < maxInterpolationDistance;

    // Don't apply linear interpolation if the DedicatedActor has just gone through a cell change, because
    // the interpolated position will be invalid, causing a slight hopping glitch
    const bool wasCellChange = hasChangedCell;
    if (shouldInterpolate && !hasChangedCell)
    {
        static const int timeMultiplier = 15;
        osg::Vec3f lerp = MechanicsHelper::getLinearInterpolation(refPos.asVec3(), position.asVec3(), dt * timeMultiplier);
        refPos.pos[0] = lerp.x();
        refPos.pos[1] = lerp.y();
        refPos.pos[2] = lerp.z();

        world->moveObject(ptr, refPos.pos[0], refPos.pos[1], refPos.pos[2]);
    }
    else
    {
        setPosition();
        hasChangedCell = false;
    }

    // First apply the exact movement input supplied by the cell authority, just
    // like EncoreMP. Some scripted/root-motion NPC paths, however, legitimately
    // arrive with a zero direction while their network target keeps moving. In
    // that case the remote CharacterController sees an idle actor and the model
    // slides. Recover locomotion from the SAME interpolation that is visibly
    // moving the DedicatedActor, without changing the packet or server state.
    setMovementSettings();

    MWMechanics::Movement *move = &ptr.getClass().getMovementSettings(ptr);
    constexpr float directionEpsilon = 0.001f;
    const bool hasNetworkTranslation =
        std::abs(direction.pos[0]) > directionEpsilon ||
        std::abs(direction.pos[1]) > directionEpsilon ||
        std::abs(direction.pos[2]) > directionEpsilon;

    if (!hasNetworkTranslation && shouldInterpolate && !wasCellChange)
    {
        const float targetDx = position.pos[0] - previousRefPos.pos[0];
        const float targetDy = position.pos[1] - previousRefPos.pos[1];
        const float targetHorizontalSquared = targetDx * targetDx + targetDy * targetDy;

        const float visibleDx = refPos.pos[0] - previousRefPos.pos[0];
        const float visibleDy = refPos.pos[1] - previousRefPos.pos[1];
        const float visibleHorizontalSquared = visibleDx * visibleDx + visibleDy * visibleDy;

        // Require both a real network target offset and visible interpolation.
        // This avoids false walk cycles from sub-pixel jitter and never converts
        // a cell change/teleport into locomotion.
        if (targetHorizontalSquared > 0.01f && visibleHorizontalSquared > 0.0001f)
        {
            const float yaw = position.rot[2];
            const float localForward = -targetDx * std::sin(yaw) + targetDy * std::cos(yaw);
            move->mPosition[0] = 0.f;
            move->mPosition[1] = localForward < -directionEpsilon ? -1.f : 1.f;
            move->mPosition[2] = 0.f;
        }
    }

    world->rotateObject(ptr, position.rot[0], position.rot[1], position.rot[2]);
}

void DedicatedActor::setMovementSettings()
{
    MWMechanics::Movement *move = &ptr.getClass().getMovementSettings(ptr);
    move->mPosition[0] = direction.pos[0];
    move->mPosition[1] = direction.pos[1];
    move->mPosition[2] = direction.pos[2];

    // Make sure the values are valid, or we'll get an infinite error loop
    if (!isnan(direction.rot[0]) && !isnan(direction.rot[1]) && !isnan(direction.rot[2]))
    {
        move->mRotation[0] = direction.rot[0];
        move->mRotation[1] = direction.rot[1];
        move->mRotation[2] = direction.rot[2];
    }
}

void DedicatedActor::setPosition()
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    world->moveObject(ptr, position.pos[0], position.pos[1], position.pos[2]);
}

void DedicatedActor::setAnimFlags()
{
    using namespace MWMechanics;

    MWMechanics::CreatureStats *ptrCreatureStats = &ptr.getClass().getCreatureStats(ptr);

    ptrCreatureStats->setDrawState(static_cast<MWMechanics::DrawState_>(drawState));

    ptrCreatureStats->setMovementFlag(CreatureStats::Flag_Run, (movementFlags & CreatureStats::Flag_Run) != 0);
    ptrCreatureStats->setMovementFlag(CreatureStats::Flag_Sneak, (movementFlags & CreatureStats::Flag_Sneak) != 0);
    ptrCreatureStats->setMovementFlag(CreatureStats::Flag_ForceJump, (movementFlags & CreatureStats::Flag_ForceJump) != 0);
    ptrCreatureStats->setMovementFlag(CreatureStats::Flag_ForceMoveJump, (movementFlags & CreatureStats::Flag_ForceMoveJump) != 0);
}

void DedicatedActor::setStatsDynamic()
{
    // Only set dynamic stats if we have received at least one packet about them
    if (!hasStatsDynamicData) return;

    MWMechanics::CreatureStats *ptrCreatureStats = &ptr.getClass().getCreatureStats(ptr);
    MWMechanics::DynamicStat<float> value;

    // Resurrect this Actor if it's not supposed to be dead according to its authority
    if (creatureStats.mDynamic[0].mCurrent > 0)
        MWBase::Environment::get().getMechanicsManager()->resurrect(ptr);

    for (int i = 0; i < 3; ++i)
    {
        value.readState(creatureStats.mDynamic[i]);
        ptrCreatureStats->setDynamic(i, value);
    }
}

void DedicatedActor::setEquipment()
{
    if (!ptr.getClass().hasInventoryStore(ptr))
        return;

    MWWorld::InventoryStore& invStore = ptr.getClass().getInventoryStore(ptr);

    for (int slot = 0; slot < MWWorld::InventoryStore::Slots; ++slot)
    {
        int count = equipmentItems[slot].count;

        // If we've somehow received a corrupted item with a count lower than 0, ignore it
        if (count < 0) continue;

        MWWorld::ContainerStoreIterator it = invStore.getSlot(slot);

        const std::string &packetRefId = equipmentItems[slot].refId;
        int packetCharge = equipmentItems[slot].charge;
        std::string storeRefId = "";
        bool equal = false;

        if (it != invStore.end())
        {
            storeRefId = it->getCellRef().getRefId();

            if (!Misc::StringUtils::ciEqual(storeRefId, packetRefId)) // if other item equiped
                invStore.unequipSlot(slot, ptr);
            else
                equal = true;
        }

        if (packetRefId.empty() || equal)
            continue;

        if (!hasItem(packetRefId, packetCharge))
        {
            ptr.getClass().getContainerStore(ptr).add(packetRefId, count, ptr);
        }

        // Equip items silently if this is the first time equipment is being set for this character
        equipItem(packetRefId, packetCharge, !hasReceivedInitialEquipment);
    }

    hasReceivedInitialEquipment = true;
}

void DedicatedActor::setAi()
{
    MWMechanics::CreatureStats *ptrCreatureStats = &ptr.getClass().getCreatureStats(ptr);
    ptrCreatureStats->setAiSetting(MWMechanics::CreatureStats::AI_Fight, 0);

    LOG_APPEND(TimedLog::LOG_VERBOSE, "- actor cellRef: %s %i-%i",
        ptr.getCellRef().getRefId().c_str(), ptr.getCellRef().getRefNum().mIndex, ptr.getCellRef().getMpNum());

    if (aiAction == mwmp::BaseActorList::CANCEL)
    {
        LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Cancelling AI sequence");

        ptrCreatureStats->getAiSequence().clear();
    }
    else if (aiAction == mwmp::BaseActorList::TRAVEL)
    {
        LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Travelling to %f, %f, %f",
            aiCoordinates.pos[0], aiCoordinates.pos[1], aiCoordinates.pos[2]);

        MWMechanics::AiTravel package(aiCoordinates.pos[0], aiCoordinates.pos[1], aiCoordinates.pos[2]);
        ptrCreatureStats->getAiSequence().stack(package, ptr, true);
    }
    else if (aiAction == mwmp::BaseActorList::WANDER)
    {
        LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Wandering for distance %i and duration %i, repetition is %s",
            aiDistance, aiDuration, aiShouldRepeat ? "true" : "false");

        std::vector<unsigned char> idleList;

        MWMechanics::AiWander package(aiDistance, aiDuration, -1, idleList, aiShouldRepeat);
        ptrCreatureStats->getAiSequence().stack(package, ptr, true);
    }
    else if (hasAiTarget)
    {
        MWWorld::Ptr targetPtr;

        if (aiTarget.isPlayer)
        {
            targetPtr = MechanicsHelper::getPlayerPtr(aiTarget);

            LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Has player target %s",
                targetPtr.getClass().getName(targetPtr).c_str());
        }
        else
        {
            if (mwmp::Main::get().getCellController()->isLocalActor(aiTarget.refNum, aiTarget.mpNum))
                targetPtr = mwmp::Main::get().getCellController()->getLocalActor(aiTarget.refNum, aiTarget.mpNum)->getPtr();
            else if (mwmp::Main::get().getCellController()->isDedicatedActor(aiTarget.refNum, aiTarget.mpNum))
                targetPtr = mwmp::Main::get().getCellController()->getDedicatedActor(aiTarget.refNum, aiTarget.mpNum)->getPtr();
            else if (aiAction == mwmp::BaseActorList::ACTIVATE)
                targetPtr = MWBase::Environment::get().getWorld()->searchPtrViaUniqueIndex(aiTarget.refNum, aiTarget.mpNum);

            if (targetPtr)
            {
                LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Has actor target %s %i-%i",
                    targetPtr.getCellRef().getRefId().c_str(), aiTarget.refNum, aiTarget.mpNum);
            }
            else
            {
                LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Has invalid actor target %i-%i",
                    aiTarget.refNum, aiTarget.mpNum);
            }

        }

        if (targetPtr)
        {
            if (aiAction == mwmp::BaseActorList::ACTIVATE)
            {
                LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Activating target");

                MWMechanics::AiActivate package(targetPtr);
                ptrCreatureStats->getAiSequence().stack(package, ptr, true);
            }

            if (aiAction == mwmp::BaseActorList::COMBAT)
            {
                LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Starting combat with target");

                MWMechanics::AiCombat package(targetPtr);
                ptrCreatureStats->getAiSequence().stack(package, ptr, true);
            }
            else if (aiAction == mwmp::BaseActorList::ESCORT)
            {
                LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Being escorted by target, for duration %i, to coordinates %f, %f, %f",
                    aiDuration, aiCoordinates.pos[0], aiCoordinates.pos[1], aiCoordinates.pos[2]);

                MWMechanics::AiEscort package(targetPtr.getCellRef().getRefId(), aiDuration,
                    aiCoordinates.pos[0], aiCoordinates.pos[1], aiCoordinates.pos[2]);
                ptrCreatureStats->getAiSequence().stack(package, ptr, true);
            }
            else if (aiAction == mwmp::BaseActorList::FOLLOW)
            {
                LOG_APPEND(TimedLog::LOG_VERBOSE, "-- Following target");

                MWMechanics::AiFollow package(targetPtr);
                package.allowAnyDistance(true);
                ptrCreatureStats->getAiSequence().stack(package, ptr, true);
            }
        }
    }
}

void DedicatedActor::playAnimation()
{
    if (!animation.groupname.empty())
    {
        std::string consumableRefId;
        if (decodeConsumableAnimation(animation.groupname, consumableRefId))
        {
            mwmp::playConsumableAnimation(ptr, consumableRefId);
            animation.groupname.clear();
            return;
        }

        std::string ambientConsumableRefId;
        if (decodeAmbientConsumableAnimation(animation.groupname, ambientConsumableRefId))
        {
            mwmp::playAmbientConsumableAnimation(ptr, ambientConsumableRefId);
            animation.groupname.clear();
            return;
        }

        InteractionAnimationData interactionData;
        if (decodeInteractionAnimation(animation.groupname, interactionData))
        {
            if (interactionData.stop)
            {
                if (mInteractionAnimationActive)
                    cancelInteractionAnimation();
                else
                    stopInteractionAnimation(ptr, interactionData);
            }
            else if (!interactionAnimationIsBlocked(ptr))
            {
                cancelInteractionAnimation();
                mInteractionAnimation = interactionData;
                mInteractionAnimationActive = playInteractionAnimation(ptr, mInteractionAnimation);
            }
            animation.groupname.clear();
            return;
        }

        MWBase::Environment::get().getMechanicsManager()->playAnimationGroup(ptr,
            animation.groupname, animation.mode, animation.count, animation.persist);

        animation.groupname.clear();
    }
}

void DedicatedActor::cancelInteractionAnimation()
{
    if (!mInteractionAnimationActive)
        return;

    stopInteractionAnimation(ptr, mInteractionAnimation);
    mInteractionAnimation = InteractionAnimationData();
    mInteractionAnimationActive = false;
}

void DedicatedActor::updateInteractionAnimation()
{
    if (!mInteractionAnimationActive)
        return;

    // Network interpolation may begin before the authority's reliable stop
    // packet arrives. Cancel locally on the first moving frame so a cosmetic
    // ArmsFolded/ArmsAtBack/gesture layer can never suppress locomotion.
    if (interactionAnimationIsBlocked(ptr))
    {
        cancelInteractionAnimation();
        return;
    }

    ensureInteractionAnimationProp(ptr, mInteractionAnimation);
}

void DedicatedActor::playSound()
{
    if (!sound.empty())
    {
        MWBase::Environment::get().getSoundManager()->say(ptr, sound);

        MWBase::WindowManager *winMgr = MWBase::Environment::get().getWindowManager();
        if (winMgr->getSubtitlesEnabled()
            && !winMgr->containsMode(MWGui::GM_Dialogue))
            winMgr->messageBox(MWBase::Environment::get().getDialogueManager()->getVoiceCaption(sound), MWGui::ShowInDialogueMode_Never);

        sound.clear();
    }
}

bool DedicatedActor::hasItem(std::string itemId, int charge)
{
    for (const auto &itemPtr : ptr.getClass().getInventoryStore(ptr))
    {
        if (::Misc::StringUtils::ciEqual(itemPtr.getCellRef().getRefId(), itemId) && itemPtr.getCellRef().getCharge() == charge)
            return true;
    }

    return false;
}

void DedicatedActor::equipItem(std::string itemId, int charge, bool noSound)
{
    for (const auto &itemPtr : ptr.getClass().getInventoryStore(ptr))
    {
        if (::Misc::StringUtils::ciEqual(itemPtr.getCellRef().getRefId(), itemId) && itemPtr.getCellRef().getCharge() == charge)
        {
            std::shared_ptr<MWWorld::Action> action = itemPtr.getClass().use(itemPtr);
            action->execute(ptr, noSound);
            break;
        }
    }
}

void DedicatedActor::addSpellsActive()
{
    MWMechanics::ActiveSpells& activeSpells = getPtr().getClass().getCreatureStats(getPtr()).getActiveSpells();

    for (const auto& activeSpell : spellsActiveChanges.activeSpells)
    {
        MWWorld::TimeStamp timestamp = MWWorld::TimeStamp(activeSpell.timestampHour, activeSpell.timestampDay);
        int casterActorId = MechanicsHelper::getActorId(activeSpell.caster);

        MechanicsHelper::createSpellGfx(getPtr(), activeSpell.params.mEffects);

        // Don't do a check for a spell's existence, because active effects from potions need to be applied here too
        activeSpells.addSpell(activeSpell.id, activeSpell.isStackingSpell, activeSpell.params.mEffects, activeSpell.params.mDisplayName, casterActorId, timestamp, false);
    }
}

void DedicatedActor::removeSpellsActive()
{
    MWMechanics::ActiveSpells& activeSpells = getPtr().getClass().getCreatureStats(getPtr()).getActiveSpells();

    for (const auto& activeSpell : spellsActiveChanges.activeSpells)
    {
        // Remove stacking spells based on their timestamps
        if (activeSpell.isStackingSpell)
        {
            MWWorld::TimeStamp timestamp = MWWorld::TimeStamp(activeSpell.timestampHour, activeSpell.timestampDay);
            activeSpells.removeSpellByTimestamp(activeSpell.id, timestamp);
        }
        else
        {
            activeSpells.removeEffects(activeSpell.id);
        }
    }
}

void DedicatedActor::setSpellsActive()
{
    MWMechanics::ActiveSpells& activeSpells = getPtr().getClass().getCreatureStats(getPtr()).getActiveSpells();
    activeSpells.clear();

    // Proceed by adding spells active
    addSpellsActive();
}

MWWorld::Ptr DedicatedActor::getPtr()
{
    return ptr;
}

void DedicatedActor::setPtr(const MWWorld::Ptr& newPtr)
{
    ptr = newPtr;

    refId = ptr.getCellRef().getRefId();
    refNum = ptr.getCellRef().getRefNum().mIndex;
    mpNum = ptr.getCellRef().getMpNum();

    position = ptr.getRefData().getPosition();
    drawState = ptr.getClass().getCreatureStats(ptr).getDrawState();
}

void DedicatedActor::reloadPtr()
{
    MWBase::World* world = MWBase::Environment::get().getWorld();
    world->disable(ptr);
    world->enable(ptr);
}
