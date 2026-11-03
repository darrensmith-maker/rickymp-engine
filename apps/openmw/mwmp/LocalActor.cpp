#include <components/openmw-mp/TimedLog.hpp>
#include <cmath>

#include "../mwbase/environment.hpp"

#include "../mwmechanics/mechanicsmanagerimp.hpp"
#include "../mwmechanics/movement.hpp"

#include "../mwrender/animation.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/worldimp.hpp"

#include "LocalActor.hpp"
#include "Main.hpp"
#include "Networking.hpp"
#include "ActorList.hpp"
#include "MechanicsHelper.hpp"

using namespace mwmp;

LocalActor::LocalActor()
{
    hasSentData = false;
    posWasChanged = false;
    equipmentChanged = false;

    wasRunning = false;
    wasSneaking = false;
    wasForceJumping = false;
    wasForceMoveJumping = false;
    wasFlying = false;

    attack.type = Attack::MELEE;
    attack.shouldSend = false;
    attack.instant = false;
    attack.pressed = false;

    cast.type = Cast::REGULAR;
    cast.shouldSend = false;
    cast.instant = false;
    cast.pressed = false;

    killer.isPlayer = false;
    killer.refId = "";
    killer.name = "";

    creatureStats.mDead = false;
    creatureStats.mDeathAnimationFinished = false;
}

LocalActor::~LocalActor()
{

}

void LocalActor::update(bool forceUpdate)
{
    updateStatsDynamic(forceUpdate);
    updateEquipment(forceUpdate, false);

    if (forceUpdate || !creatureStats.mDeathAnimationFinished)
    {
        updatePosition(forceUpdate);
        updateAnimFlags(forceUpdate);
        updateAnimPlay();
        updateSpeech();
        updateAttackOrCast();
    }

    hasSentData = true;
}

void LocalActor::updateCell()
{
    LOG_MESSAGE_SIMPLE(TimedLog::LOG_VERBOSE, "Sending ID_ACTOR_CELL_CHANGE about %s %i-%i in cell %s to server",
                       refId.c_str(), refNum, mpNum, cell.getShortDescription().c_str());

    LOG_APPEND(TimedLog::LOG_VERBOSE, "- Moved to cell %s", ptr.getCell()->getCell()->getShortDescription().c_str());

    cell = *ptr.getCell()->getCell();
    position = ptr.getRefData().getPosition();
    isFollowerCellChange = false;

    mwmp::Main::get().getNetworking()->getActorList()->addCellChangeActor(*this);
}

void LocalActor::updatePosition(bool forceUpdate)
{
    const ESM::Position ptrPosition = ptr.getRefData().getPosition();
    const float dx = ptrPosition.pos[0] - position.pos[0];
    const float dy = ptrPosition.pos[1] - position.pos[1];
    const float dz = ptrPosition.pos[2] - position.pos[2];
    const float positionDeltaSquared = dx * dx + dy * dy + dz * dz;
    const bool actualPositionChanged = positionDeltaSquared > 0.0625f; // ignore sub-0.25-unit jitter

    bool posIsChanging = false;

    if (creatureStats.mDead)
    {
        posIsChanging = actualPositionChanged;
    }
    else
    {
        const float directionEpsilon = 0.001f;
        const bool hasTranslationDirection =
            std::abs(direction.pos[0]) > directionEpsilon ||
            std::abs(direction.pos[1]) > directionEpsilon ||
            std::abs(direction.pos[2]) > directionEpsilon;

        posIsChanging = hasTranslationDirection || actualPositionChanged ||
            std::abs(direction.rot[0]) > directionEpsilon ||
            std::abs(direction.rot[1]) > directionEpsilon ||
            std::abs(direction.rot[2]) > directionEpsilon ||
            !MWBase::Environment::get().getWorld()->isOnGround(ptr);

        // Keep the movement vector exactly as CharacterController produced it.
        // EncoreMP treats direction as authoritative movement input; synthesizing
        // it here made packet state depend on network/update timing. We still use
        // actualPositionChanged above so scripted/root-motion movement continues
        // to send ActorPosition packets even when CharacterController has no input.
    }

    if (forceUpdate || posIsChanging || posWasChanged)
    {
        posWasChanged = posIsChanging;
        position = ptrPosition;
        mwmp::Main::get().getNetworking()->getActorList()->addPositionActor(*this);
    }
}

void LocalActor::updateAnimFlags(bool forceUpdate)
{
    MWBase::World *world = MWBase::Environment::get().getWorld();
    MWMechanics::CreatureStats ptrCreatureStats = ptr.getClass().getCreatureStats(ptr);

    using namespace MWMechanics;

    bool isRunning = ptrCreatureStats.getMovementFlag(CreatureStats::Flag_Run);
    bool isSneaking = ptrCreatureStats.getMovementFlag(CreatureStats::Flag_Sneak)
        || ptrCreatureStats.getMovementFlag(CreatureStats::Flag_ForceSneak);
    bool isForceJumping = ptrCreatureStats.getMovementFlag(CreatureStats::Flag_ForceJump);
    bool isForceMoveJumping = ptrCreatureStats.getMovementFlag(CreatureStats::Flag_ForceMoveJump);

    isFlying = world->isFlying(ptr);

    MWMechanics::DrawState_ currentDrawState = ptr.getClass().getCreatureStats(ptr).getDrawState();

    if (wasRunning != isRunning || wasSneaking != isSneaking ||
        wasForceJumping != isForceJumping || wasForceMoveJumping != isForceMoveJumping ||
        lastDrawState != currentDrawState || wasFlying != isFlying ||
        forceUpdate)
    {

        wasRunning = isRunning;
        wasSneaking = isSneaking;
        wasForceJumping = isForceJumping;
        wasForceMoveJumping = isForceMoveJumping;
        lastDrawState = currentDrawState;

        wasFlying = isFlying;

        movementFlags = 0;

#define __SETFLAG(flag, value) (value) ? (movementFlags | flag) : (movementFlags & ~flag)

        movementFlags = __SETFLAG(CreatureStats::Flag_Sneak, isSneaking);
        movementFlags = __SETFLAG(CreatureStats::Flag_Run, isRunning);
        movementFlags = __SETFLAG(CreatureStats::Flag_ForceJump, isForceJumping);
        movementFlags = __SETFLAG(CreatureStats::Flag_ForceMoveJump, isForceMoveJumping);

#undef __SETFLAG

        drawState = currentDrawState;

        mwmp::Main::get().getNetworking()->getActorList()->addAnimFlagsActor(*this);
    }
}

void LocalActor::updateAnimPlay()
{
    if (!animation.groupname.empty())
    {
        mwmp::Main::get().getNetworking()->getActorList()->addAnimPlayActor(*this);
        animation.groupname.clear();
    }
}

void LocalActor::updateSpeech()
{
    if (!sound.empty())
    {
        mwmp::Main::get().getNetworking()->getActorList()->addSpeechActor(*this);
        sound.clear();
    }
}

void LocalActor::updateStatsDynamic(bool forceUpdate)
{
    MWMechanics::CreatureStats *ptrCreatureStats = &ptr.getClass().getCreatureStats(ptr);
    MWMechanics::DynamicStat<float> health(ptrCreatureStats->getHealth());
    MWMechanics::DynamicStat<float> magicka(ptrCreatureStats->getMagicka());
    MWMechanics::DynamicStat<float> fatigue(ptrCreatureStats->getFatigue());

    // Update stats when they become 0 or they have changed enough
    //
    // Also check for an oldHealth of 0 changing to something else for resurrected NPCs

    auto needUpdate = [](MWMechanics::DynamicStat<float> &oldVal, MWMechanics::DynamicStat<float> &newVal, int limit) {
        return oldVal != newVal && (newVal.getCurrent() == 0 || oldVal.getCurrent() == 0
                                    || abs(oldVal.getCurrent() - newVal.getCurrent()) >= limit);
    };

    if (forceUpdate || needUpdate(oldHealth, health, 3) || needUpdate(oldMagicka, magicka, 7) ||
        needUpdate(oldFatigue, fatigue, 7))
    {
        oldHealth = health;
        oldMagicka = magicka;
        oldFatigue = fatigue;

        health.writeState(creatureStats.mDynamic[0]);
        magicka.writeState(creatureStats.mDynamic[1]);
        fatigue.writeState(creatureStats.mDynamic[2]);

        creatureStats.mDead = ptrCreatureStats->isDead();
        creatureStats.mDeathAnimationFinished = ptrCreatureStats->isDeathAnimationFinished();

        mwmp::Main::get().getNetworking()->getActorList()->addStatsDynamicActor(*this);
    }
}

void LocalActor::updateEquipment(bool forceUpdate, bool sendImmediately)
{
    if (!ptr.getClass().hasInventoryStore(ptr))
        return;

    MWWorld::InventoryStore &invStore = ptr.getClass().getInventoryStore(ptr);
    
    // If we've never sent any data, autoEquip the actor just in case its inventory
    // slots have been cleared by a previous Container packet
    if (!hasSentData)
        invStore.autoEquip(ptr);

    if (forceUpdate)
        equipmentChanged = true;

    for (int slot = 0; slot < MWWorld::InventoryStore::Slots; slot++)
    {
        auto &item = equipmentItems[slot];
        MWWorld::ContainerStoreIterator it = invStore.getSlot(slot);

        if (it != invStore.end())
        {
            auto &cellRef = it->getCellRef();
            if (!::Misc::StringUtils::ciEqual(cellRef.getRefId(), item.refId))
            {
                equipmentChanged = true;

                item.refId = cellRef.getRefId();
                item.charge = cellRef.getCharge();
                item.enchantmentCharge = it->getCellRef().getEnchantmentCharge();
                item.count = it->getRefData().getCount();
            }
        }
        else if (!item.refId.empty())
        {
            equipmentChanged = true;
            item.refId = "";
            item.count = 0;
            item.charge = -1;
            item.enchantmentCharge = -1;
        }
    }

    if (equipmentChanged)
    {
        if (sendImmediately)
            sendEquipment();
        else
            mwmp::Main::get().getNetworking()->getActorList()->addEquipmentActor(*this);

        equipmentChanged = false;
    }
}

void LocalActor::updateAttackOrCast()
{
    if (attack.shouldSend)
    {
        mwmp::Main::get().getNetworking()->getActorList()->addAttackActor(*this);
        attack.shouldSend = false;
    }
    else if (cast.shouldSend)
    {
        mwmp::Main::get().getNetworking()->getActorList()->addCastActor(*this);
        cast.shouldSend = false;
        cast.hasProjectile = false;
    }
}

void LocalActor::sendEquipment()
{
    ActorList actorList;
    actorList.cell = cell;
    actorList.addActor(*this);
    Main::get().getNetworking()->getActorPacket(ID_ACTOR_EQUIPMENT)->setActorList(&actorList);
    Main::get().getNetworking()->getActorPacket(ID_ACTOR_EQUIPMENT)->Send();
}

void LocalActor::sendSpellsActiveAddition(const std::string id, bool isStackingSpell, const MWMechanics::ActiveSpells::ActiveSpellParams& params)
{
    // Skip any bugged spells that somehow have clientside-only dynamic IDs
    if (id.find("$dynamic") != std::string::npos)
        return;

    spellsActiveChanges.activeSpells.clear();

    const MWWorld::Ptr& caster = MWBase::Environment::get().getWorld()->searchPtrViaActorId(params.mCasterActorId);

    mwmp::ActiveSpell spell;
    spell.id = id;
    spell.isStackingSpell = isStackingSpell;
    spell.caster = MechanicsHelper::getTarget(caster);
    spell.timestampDay = params.mTimeStamp.getDay();
    spell.timestampHour = params.mTimeStamp.getHour();
    spell.params.mEffects = params.mEffects;
    spell.params.mDisplayName = params.mDisplayName;
    spellsActiveChanges.activeSpells.push_back(spell);

    spellsActiveChanges.action = mwmp::SpellsActiveChanges::ADD;

    ActorList actorList;
    actorList.cell = cell;
    actorList.addActor(*this);
    Main::get().getNetworking()->getActorPacket(ID_ACTOR_SPELLS_ACTIVE)->setActorList(&actorList);
    Main::get().getNetworking()->getActorPacket(ID_ACTOR_SPELLS_ACTIVE)->Send();
}

void LocalActor::sendSpellsActiveRemoval(const std::string id, bool isStackingSpell, MWWorld::TimeStamp timestamp)
{
    // Skip any bugged spells that somehow have clientside-only dynamic IDs
    if (id.find("$dynamic") != std::string::npos)
        return;

    spellsActiveChanges.activeSpells.clear();

    mwmp::ActiveSpell spell;
    spell.id = id;
    spell.isStackingSpell = isStackingSpell;
    spell.timestampDay = timestamp.getDay();
    spell.timestampHour = timestamp.getHour();
    spellsActiveChanges.activeSpells.push_back(spell);

    spellsActiveChanges.action = mwmp::SpellsActiveChanges::REMOVE;

    ActorList actorList;
    actorList.cell = cell;
    actorList.addActor(*this);
    Main::get().getNetworking()->getActorPacket(ID_ACTOR_SPELLS_ACTIVE)->setActorList(&actorList);
    Main::get().getNetworking()->getActorPacket(ID_ACTOR_SPELLS_ACTIVE)->Send();
}

void LocalActor::sendDeath(char newDeathState)
{
    deathState = newDeathState;

    if (MechanicsHelper::isEmptyTarget(killer))
        killer = MechanicsHelper::getTarget(ptr);

    LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Sending ID_ACTOR_DEATH about %s %i-%i in cell %s to server\n- deathState: %d",
        refId.c_str(), refNum, mpNum, cell.getShortDescription().c_str(), deathState);

    ActorList actorList;
    actorList.cell = cell;
    actorList.addActor(*this);
    Main::get().getNetworking()->getActorPacket(ID_ACTOR_DEATH)->setActorList(&actorList);
    Main::get().getNetworking()->getActorPacket(ID_ACTOR_DEATH)->Send();

    MechanicsHelper::clearTarget(killer);
}

MWWorld::Ptr LocalActor::getPtr()
{
    return ptr;
}

void LocalActor::setPtr(const MWWorld::Ptr& newPtr)
{
    ptr = newPtr;

    refId = ptr.getCellRef().getRefId();
    refNum = ptr.getCellRef().getRefNum().mIndex;
    mpNum = ptr.getCellRef().getMpNum();

    lastDrawState = ptr.getClass().getCreatureStats(ptr).getDrawState();
    oldHealth = ptr.getClass().getCreatureStats(ptr).getHealth();
    oldMagicka = ptr.getClass().getCreatureStats(ptr).getMagicka();
    oldFatigue = ptr.getClass().getCreatureStats(ptr).getFatigue();
}
