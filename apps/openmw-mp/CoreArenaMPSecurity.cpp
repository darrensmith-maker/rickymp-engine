#include "CoreArenaMPSecurity.hpp"

#include "Player.hpp"
#include "Cell.hpp"
#include "CellController.hpp"

#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr float kHugeFloat = 100000000.f;
    constexpr double kHugeDouble = 100000000.0;
    constexpr int kHugeInt = 100000000;
    constexpr std::size_t kMaxSmallList = 1024;
    constexpr std::size_t kMaxIdLength = 256;

    struct RateWindow
    {
        Clock::time_point started = Clock::now();
        unsigned count = 0;
    };

    struct InteractionLock
    {
        std::uint64_t ownerGuid = 0;
        Clock::time_point touched = Clock::time_point::min();
    };

    struct SecurityState
    {
        bool positionInitialized = false;
        ESM::Position acceptedPosition{};
        ESM::Position acceptedDirection{};
        Clock::time_point positionTime = Clock::now();
        Clock::time_point lastSpeedStrike = Clock::time_point::min();
        unsigned speedStrikes = 0;

        // Current authoritative C++ state captured immediately before an
        // incoming client packet overwrites the corresponding fields.
        std::uint8_t snapshotPacket = 0;
        double attributeBase[8]{};
        double skillBase[27]{};
        double dynamicBase[3]{};
        int level = 0;
        int bounty = 0;
        int reputation = 0;

        std::unordered_map<std::uint8_t, RateWindow> rateWindows;
        std::string interactionKey;
        std::uint8_t interactionPacket = 0;
        Clock::time_point interactionTime = Clock::time_point::min();
    };

    std::mutex sMutex;
    std::unordered_map<std::uint64_t, SecurityState> sStates;
    std::unordered_map<std::string, InteractionLock> sInteractionLocks;

    template <class T>
    bool boundedSigned(T value, T limit)
    {
        return value >= -limit && value <= limit;
    }

    bool finiteBounded(float value, float limit = kHugeFloat)
    {
        return std::isfinite(value) && std::abs(value) <= limit;
    }

    bool finiteBounded(double value, double limit = kHugeDouble)
    {
        return std::isfinite(value) && std::abs(value) <= limit;
    }

    bool finiteNonNegative(float value, float limit = kHugeFloat)
    {
        return std::isfinite(value) && value >= 0.f && value <= limit;
    }

    bool finiteNonNegative(double value, double limit = kHugeDouble)
    {
        return std::isfinite(value) && value >= 0.0 && value <= limit;
    }

    bool saneId(const std::string& value, bool allowEmpty = false, std::size_t maxLength = kMaxIdLength)
    {
        return value.size() <= maxLength && (allowEmpty || !value.empty()) && value.find('\0') == std::string::npos;
    }

    bool saneCell(const ESM::Cell& cell)
    {
        constexpr int allowedFlags = ESM::Cell::Interior | ESM::Cell::HasWater | ESM::Cell::NoSleep | ESM::Cell::QuasiEx;
        return (cell.mData.mFlags & ~allowedFlags) == 0
            && boundedSigned(cell.mData.mX, 1000000) && boundedSigned(cell.mData.mY, 1000000)
            && cell.mName.size() <= 512 && cell.mRegion.size() <= 512;
    }

    bool finitePosition(const ESM::Position& position)
    {
        for (int i = 0; i < 3; ++i)
        {
            if (!finiteBounded(position.pos[i]) || !finiteBounded(position.rot[i], 1000000.f))
                return false;
        }
        return true;
    }

    bool finiteVector3(const float* value, float limit = kHugeFloat)
    {
        return finiteBounded(value[0], limit) && finiteBounded(value[1], limit) && finiteBounded(value[2], limit);
    }

    bool finiteProjectile(const mwmp::ProjectileOrigin& projectile)
    {
        if (!finiteVector3(projectile.origin))
            return false;
        for (int i = 0; i < 4; ++i)
            if (!finiteBounded(projectile.orientation[i], 1000000.f))
                return false;
        return true;
    }

    bool saneTarget(const mwmp::Target& target, bool allowEmptyNpc = false)
    {
        if (target.isPlayer)
            return target.guid.g != 0;

        // Empty targets are legitimate for actions such as beginning a swing,
        // self/untargeted casting and some death causes. Ignore numeric target
        // identity in that case because older client code can leave refNum/mpNum
        // at sentinel values while refId is empty.
        if (target.refId.empty())
            return allowEmptyNpc && target.name.size() <= 256;

        if (!saneId(target.refId) || target.name.size() > 256)
            return false;
        return !(target.refNum != 0 && target.mpNum != 0);
    }

    template <class T>
    bool saneStatState(const ESM::StatState<T>& stat, double baseLimit, bool allowNegativeCurrent)
    {
        const double base = static_cast<double>(stat.mBase);
        const double mod = static_cast<double>(stat.mMod);
        const double current = static_cast<double>(stat.mCurrent);
        return std::isfinite(base) && std::isfinite(mod) && std::isfinite(current)
            && base >= 0.0 && base <= baseLimit
            && std::abs(mod) <= baseLimit * 10.0
            && (allowNegativeCurrent ? std::abs(current) <= baseLimit * 10.0
                                     : (current >= 0.0 && current <= baseLimit * 10.0))
            && finiteNonNegative(stat.mDamage, static_cast<float>(baseLimit * 10.0))
            && finiteNonNegative(stat.mProgress, static_cast<float>(baseLimit * 10.0));
    }


    bool saneCharge(int value)
    {
        // -1 is the TES3/OpenMW sentinel for "use the item's default/max charge".
        return value == -1 || (value >= 0 && value <= kHugeInt);
    }

    template <class T>
    bool saneEnchantmentCharge(T value)
    {
        const double v = static_cast<double>(value);
        return std::isfinite(v) && (v == -1.0 || (v >= 0.0 && v <= kHugeDouble));
    }

    bool saneInventoryItem(const mwmp::Item& item, bool allowEmpty = false)
    {
        if (!saneId(item.refId, allowEmpty) || item.soul.size() > 256)
            return false;
        if (item.refId.empty())
            return allowEmpty && item.count == 0;
        return item.count > 0 && item.count <= 1000000
            && saneCharge(item.charge)
            && saneEnchantmentCharge(item.enchantmentCharge);
    }

    bool saneContainerItem(const mwmp::ContainerItem& item)
    {
        return saneId(item.refId) && item.soul.size() <= 256
            && item.count > 0 && item.count <= 1000000
            && item.actionCount >= 0 && item.actionCount <= 1000000
            && saneCharge(item.charge)
            && saneEnchantmentCharge(item.enchantmentCharge);
    }

    bool saneActiveSpells(const mwmp::SpellsActiveChanges& changes)
    {
        if (changes.action < mwmp::SpellsActiveChanges::SET || changes.action > mwmp::SpellsActiveChanges::REMOVE
            || changes.activeSpells.size() > 512)
            return false;

        for (const mwmp::ActiveSpell& spell : changes.activeSpells)
        {
            if (!saneId(spell.id) || spell.timestampDay < 0 || spell.timestampDay > 10000000
                || !finiteNonNegative(spell.timestampHour, 24.01))
                return false;

            // ArenaMP's current client value-initializes REMOVE records. The
            // protocol still serializes display/caster/effectCount for REMOVE,
            // so validate them too instead of leaving a numeric smuggling path.
            if (spell.params.mDisplayName.size() > 512 || !saneTarget(spell.caster, true)
                || spell.params.mEffects.size() > 256)
                return false;

            for (const ESM::ActiveEffect& effect : spell.params.mEffects)
            {
                // Negative magnitude is legitimate for some inverse/absorb
                // effects. Duration/timeLeft are never legitimately negative.
                if (effect.mEffectId < 0 || effect.mEffectId > 100000
                    || effect.mArg < -1 || effect.mArg > 100000
                    || !finiteBounded(effect.mMagnitude, 1000000.f)
                    || !finiteNonNegative(effect.mDuration, 10000000.f)
                    || !finiteNonNegative(effect.mTimeLeft, 10000000.f)
                    || effect.mTimeLeft > effect.mDuration + 5.f)
                    return false;
            }
        }
        return true;
    }

    bool saneEffectList(const ESM::EffectList& list)
    {
        if (list.mList.size() > 256)
            return false;
        for (const ESM::ENAMstruct& effect : list.mList)
        {
            if (effect.mEffectID < 0 || effect.mEffectID > 10000
                || effect.mSkill < -1 || effect.mSkill >= 27
                || effect.mAttribute < -1 || effect.mAttribute >= 8
                || effect.mRange < 0 || effect.mRange > 2
                || effect.mArea < 0 || effect.mArea > 1000000
                || effect.mDuration < 0 || effect.mDuration > 10000000
                || effect.mMagnMin < 0 || effect.mMagnMin > 1000000
                || effect.mMagnMax < 0 || effect.mMagnMax > 1000000
                || effect.mMagnMin > effect.mMagnMax)
                return false;
        }
        return true;
    }

    bool sanePartList(const ESM::PartReferenceList& parts)
    {
        if (parts.mParts.size() > 27)
            return false;
        for (const ESM::PartReference& part : parts.mParts)
        {
            if (part.mPart >= ESM::PRT_Count || part.mMale.size() > 256 || part.mFemale.size() > 256)
                return false;
        }
        return true;
    }

    bool saneAttack(const mwmp::Attack& attack)
    {
        if (!saneTarget(attack.target, true) || (attack.type != mwmp::Attack::MELEE && attack.type != mwmp::Attack::RANGED))
            return false;
        if (attack.attackAnimation.size() > 128 || attack.rangedWeaponId.size() > 256 || attack.rangedAmmoId.size() > 256)
            return false;
        if (attack.type == mwmp::Attack::RANGED)
        {
            if (!finiteNonNegative(attack.attackStrength, 10.f) || !saneId(attack.rangedWeaponId)
                || !saneId(attack.rangedAmmoId) || !finiteProjectile(attack.projectileOrigin))
                return false;
        }
        if (attack.isHit && (!finiteNonNegative(attack.damage, 1000000.f) || !finiteVector3(attack.hitPosition.pos)))
            return false;
        return true;
    }

    bool saneCast(const mwmp::Cast& cast)
    {
        if (!saneTarget(cast.target, true) || (cast.type != mwmp::Cast::REGULAR && cast.type != mwmp::Cast::ITEM))
            return false;
        if (cast.type == mwmp::Cast::ITEM)
        {
            if (!saneId(cast.itemId))
                return false;
        }
        else if (!saneId(cast.spellId))
            return false;
        return !cast.hasProjectile || finiteProjectile(cast.projectileOrigin);
    }

    bool saneAnimation(const mwmp::Animation& animation)
    {
        return animation.groupname.size() <= 128
            && animation.mode >= 0 && animation.mode <= 16
            && animation.count >= 0 && animation.count <= 1000;
    }

    bool saneBaseObjectIdentity(const mwmp::BaseObject& object, bool allowPlayer)
    {
        if (allowPlayer && object.isPlayer)
            return object.guid.g != 0;
        return saneId(object.refId) && !(object.refNum != 0 && object.mpNum != 0);
    }

    bool finiteObject(const mwmp::BaseObject& object, std::uint8_t packetId)
    {
        // These packets intentionally do not serialize an object identity.
        // Validate only the fields actually present on the wire so we do not
        // reject legitimate packets because refId/refNum/mpNum were untouched.
        if (packetId == ID_MUSIC_PLAY)
            return object.musicFilename.size() <= 1024;
        if (packetId == ID_VIDEO_PLAY)
            return object.videoFilename.size() <= 1024;
        if (packetId == ID_SCRIPT_MEMBER_SHORT)
            return true; // legacy placeholder packet has no payload fields

        if (object.refId.size() > 256 || object.topicId.size() > 512)
            return false;

        // Only packets that explicitly serialize isPlayer may safely use it.
        const bool packetHasObjectPlayerFlag = packetId == ID_OBJECT_ACTIVATE || packetId == ID_OBJECT_HIT
            || packetId == ID_OBJECT_SOUND || packetId == ID_CONSOLE_COMMAND;
        if (!saneBaseObjectIdentity(object, packetHasObjectPlayerFlag))
            return false;

        switch (packetId)
        {
            case ID_OBJECT_MOVE:
                if (!finiteVector3(object.position.pos)) return false;
                break;
            case ID_OBJECT_ROTATE:
                if (!finiteBounded(object.position.rot[0], 1000000.f)
                    || !finiteBounded(object.position.rot[1], 1000000.f)
                    || !finiteBounded(object.position.rot[2], 1000000.f)) return false;
                break;
            case ID_OBJECT_PLACE:
            case ID_OBJECT_SPAWN:
                if (!finitePosition(object.position)) return false;
                break;
            case ID_OBJECT_TRAP:
                if (!object.isDisarmed && !finitePosition(object.position)) return false;
                break;
            default: break;
        }

        if (packetId == ID_OBJECT_PLACE)
        {
            if (object.count <= 0 || object.count > 1000000 || !saneCharge(object.charge)
                || !saneEnchantmentCharge(object.enchantmentCharge) || object.goldValue < 0 || object.goldValue > kHugeInt
                || object.soul.size() > 256)
                return false;
        }
        else if (packetId == ID_OBJECT_SCALE)
        {
            if (!finiteNonNegative(object.scale, 1000.f) || object.scale <= 0.f) return false;
        }
        else if (packetId == ID_OBJECT_LOCK)
        {
            if (object.lockLevel < -1000 || object.lockLevel > 1000) return false;
        }
        else if (packetId == ID_OBJECT_SOUND)
        {
            if (!saneId(object.soundId) || !finiteNonNegative(object.volume, 100.f) || !finiteNonNegative(object.pitch, 100.f))
                return false;
        }
        else if (packetId == ID_OBJECT_ANIM_PLAY)
        {
            if (object.animGroup.size() > 128 || object.animMode < 0 || object.animMode > 16) return false;
        }
        else if (packetId == ID_OBJECT_HIT)
        {
            if (!saneTarget(object.hittingActor)) return false;
            // Damage/block/knockdown are serialized only for successful hits.
            if (object.hitAttack.success && !finiteNonNegative(object.hitAttack.damage, 1000000.f)) return false;
        }
        else if (packetId == ID_OBJECT_SPAWN && object.isSummon)
        {
            if (object.summonEffectId < 0 || object.summonEffectId > 100000 || !saneId(object.summonSpellId)
                || !finiteNonNegative(object.summonDuration, 10000000.f) || !saneTarget(object.master)) return false;
        }
        else if (packetId == ID_OBJECT_MISCELLANEOUS)
        {
            if (object.goldPool > 100000000u || !finiteNonNegative(object.lastGoldRestockHour, 24.01f)
                || object.lastGoldRestockDay < 0 || object.lastGoldRestockDay > 10000000) return false;
        }
        else if (packetId == ID_OBJECT_DIALOGUE_CHOICE)
        {
            if (object.dialogueChoiceType > mwmp::DialogueChoiceType::REPAIR || object.guiId < 0 || object.guiId > 1000000)
                return false;
            if (object.dialogueChoiceType == mwmp::DialogueChoiceType::TOPIC && object.topicId.empty()) return false;
        }
        else if (packetId == ID_DOOR_STATE)
        {
            if (object.doorState < 0 || object.doorState > 2) return false;
        }
        else if (packetId == ID_DOOR_DESTINATION && object.teleportState)
        {
            if (!finiteVector3(object.destinationPosition.pos)
                || !finiteBounded(object.destinationPosition.rot[0], 1000000.f)
                || !finiteBounded(object.destinationPosition.rot[2], 1000000.f)) return false;
        }
        else if (packetId == ID_CLIENT_SCRIPT_LOCAL)
        {
            if (object.clientLocals.size() > 1024) return false;
            for (const mwmp::ClientVariable& variable : object.clientLocals)
            {
                if (variable.internalIndex < 0 || variable.internalIndex > 1000000
                    || variable.variableType < mwmp::VARIABLE_TYPE::SHORT || variable.variableType > mwmp::VARIABLE_TYPE::FLOAT)
                    return false;
                if (variable.variableType == mwmp::VARIABLE_TYPE::FLOAT && !finiteBounded(variable.floatValue)) return false;
                if ((variable.variableType == mwmp::VARIABLE_TYPE::SHORT || variable.variableType == mwmp::VARIABLE_TYPE::LONG)
                    && !boundedSigned(variable.intValue, kHugeInt)) return false;
            }
        }
        else if (packetId == ID_OBJECT_ACTIVATE)
        {
            if (!saneTarget(object.activatingActor)) return false;
        }
        else if (packetId == ID_CONTAINER)
        {
            if (object.containerItems.size() > 1024) return false;
            for (const mwmp::ContainerItem& item : object.containerItems)
                if (!saneContainerItem(item)) return false;
        }

        return true;
    }

    bool saneActorIdentity(const mwmp::BaseActor& actor, std::uint8_t packetId)
    {
        if (actor.refNum != 0 && actor.mpNum != 0)
            return false;

        // ActorList and ActorDeath explicitly serialize refId. Most actor
        // update packets serialize only refNum/mpNum, so requiring refId there
        // would reject every legitimate movement/stat packet.
        if (packetId == ID_ACTOR_LIST || packetId == ID_ACTOR_DEATH)
            return saneId(actor.refId);

        return actor.refNum != 0 || actor.mpNum != 0;
    }

    bool saneActor(const mwmp::BaseActor& actor, std::uint8_t packetId)
    {
        // PacketActorTest is a legacy empty payload and deliberately overrides
        // Actor() without serializing refNum/mpNum. Its packet/header bounds and
        // cell authority are still validated by ValidateActorPacket.
        if (packetId == ID_ACTOR_TEST)
            return true;

        if (!saneActorIdentity(actor, packetId))
            return false;

        switch (packetId)
        {
            case ID_ACTOR_POSITION:
                return finitePosition(actor.position) && finitePosition(actor.direction);
            case ID_ACTOR_CELL_CHANGE:
                return saneCell(actor.cell) && finitePosition(actor.position) && finitePosition(actor.direction);
            case ID_ACTOR_STATS_DYNAMIC:
                return saneStatState(actor.creatureStats.mDynamic[0], 10000000.0, true)
                    && saneStatState(actor.creatureStats.mDynamic[1], 10000000.0, true)
                    && saneStatState(actor.creatureStats.mDynamic[2], 10000000.0, true);
            case ID_ACTOR_ATTACK:
                return saneAttack(actor.attack);
            case ID_ACTOR_CAST:
                return saneCast(actor.cast);
            case ID_ACTOR_SPELLS_ACTIVE:
                return saneActiveSpells(actor.spellsActiveChanges);
            case ID_ACTOR_ANIM_PLAY:
                return saneAnimation(actor.animation);
            case ID_ACTOR_ANIM_FLAGS:
                return (actor.movementFlags & ~63u) == 0u && actor.drawState >= 0 && actor.drawState <= 4;
            case ID_ACTOR_EQUIPMENT:
                for (const mwmp::Item& item : actor.equipmentItems)
                    if (!saneInventoryItem(item, true)) return false;
                return true;
            case ID_ACTOR_AI:
                if (actor.aiAction > mwmp::BaseActorList::WANDER) return false;
                // Validate only fields that PacketActorAI actually serialized
                // for this AI action; the rest of BaseActor is intentionally
                // unspecified on receive.
                if (actor.aiAction == mwmp::BaseActorList::WANDER && actor.aiDistance > 1000000u) return false;
                if ((actor.aiAction == mwmp::BaseActorList::ESCORT || actor.aiAction == mwmp::BaseActorList::WANDER)
                    && actor.aiDuration > 10000000u) return false;
                if ((actor.aiAction == mwmp::BaseActorList::ESCORT || actor.aiAction == mwmp::BaseActorList::TRAVEL)
                    && !finitePosition(actor.aiCoordinates)) return false;
                if (actor.aiAction == mwmp::BaseActorList::ACTIVATE || actor.aiAction == mwmp::BaseActorList::COMBAT
                    || actor.aiAction == mwmp::BaseActorList::ESCORT || actor.aiAction == mwmp::BaseActorList::FOLLOW)
                    return !actor.hasAiTarget || saneTarget(actor.aiTarget);
                return true;
            case ID_ACTOR_DEATH:
                return actor.deathState >= 0 && actor.deathState <= 16 && saneTarget(actor.killer, true);
            case ID_ACTOR_SPEECH:
                return actor.sound.size() <= 512;
            default:
                return true;
        }
    }

    unsigned packetRateLimit(std::uint8_t packetId)
    {
        switch (packetId)
        {
            case ID_OBJECT_ACTIVATE: return 16;
            case ID_OBJECT_DIALOGUE_CHOICE: return 24;
            case ID_CONTAINER: return 64;
            case ID_PLAYER_INVENTORY: return 96;
            case ID_PLAYER_ATTRIBUTE:
            case ID_PLAYER_SKILL:
            case ID_PLAYER_STATS_DYNAMIC:
            case ID_PLAYER_SPELLS_ACTIVE: return 64;
            case ID_PLAYER_ATTACK:
            case ID_PLAYER_CAST: return 96;
            default: return 256;
        }
    }

    bool checkRate(Player& player, std::uint8_t packetId, unsigned forcedLimit = 0)
    {
        const unsigned limit = forcedLimit != 0 ? forcedLimit : packetRateLimit(packetId);
        if (limit == 0) return true;
        const Clock::time_point now = Clock::now();
        std::lock_guard<std::mutex> lock(sMutex);
        SecurityState& state = sStates[player.guid.g];
        RateWindow& window = state.rateWindows[packetId];
        if (std::chrono::duration<double>(now - window.started).count() >= 1.0)
        {
            window.started = now;
            window.count = 0;
        }
        return ++window.count <= limit;
    }

    std::string interactionKeyForObject(const mwmp::BaseObjectList& list, const mwmp::BaseObject& object,
        std::uint8_t packetId)
    {
        if (packetId == ID_OBJECT_ACTIVATE && object.isPlayer)
            return list.cell.getShortDescription() + "|player|" + std::to_string(object.guid.g);
        return list.cell.getShortDescription() + "|" + object.refId + "|"
            + std::to_string(object.refNum) + "|" + std::to_string(object.mpNum);
    }

    std::string interactionKey(const mwmp::BaseObjectList& list, std::uint8_t packetId)
    {
        if (list.baseObjects.empty()) return {};
        return interactionKeyForObject(list, list.baseObjects.front(), packetId);
    }

    const char* packetName(std::uint8_t packetId)
    {
        switch (packetId)
        {
            case ID_OBJECT_ACTIVATE: return "ID_OBJECT_ACTIVATE";
            case ID_OBJECT_DIALOGUE_CHOICE: return "ID_OBJECT_DIALOGUE_CHOICE";
            case ID_CONTAINER: return "ID_CONTAINER";
            default: return "packet";
        }
    }

    bool saneClientCreatedDynamicRecords(const mwmp::BaseWorldstate& worldstate)
    {
        if (worldstate.recordsCount > 256)
            return false;

        auto saneCommonStrings = [](const auto& recordData) {
            return recordData.mId.size() <= 256 && !recordData.mId.empty();
        };
        auto saneRecordBase = [](const auto& record) {
            return record.baseId.size() <= 256;
        };

        switch (worldstate.recordsType)
        {
            case mwmp::RECORD_TYPE::SPELL:
                for (const auto& record : worldstate.spellRecords)
                {
                    const auto& d = record.data;
                    if (!saneRecordBase(record) || !saneCommonStrings(d) || d.mName.size() > 512
                        || d.mData.mType < ESM::Spell::ST_Spell || d.mData.mType > ESM::Spell::ST_Power
                        || d.mData.mCost < 0 || d.mData.mCost > 1000000
                        || d.mData.mFlags < 0 || (d.mData.mFlags & ~7) != 0
                        || !saneEffectList(d.mEffects)) return false;
                }
                return true;

            case mwmp::RECORD_TYPE::POTION:
                for (const auto& record : worldstate.potionRecords)
                {
                    const auto& d = record.data;
                    if (!saneRecordBase(record) || !saneCommonStrings(d) || record.quantity > 1000000u
                        || d.mName.size() > 512 || d.mModel.size() > 1024 || d.mIcon.size() > 1024 || d.mScript.size() > 256
                        || !finiteNonNegative(d.mData.mWeight, 1000000.f)
                        || d.mData.mValue < 0 || d.mData.mValue > kHugeInt
                        || d.mData.mAutoCalc < 0 || d.mData.mAutoCalc > 1
                        || !saneEffectList(d.mEffects)) return false;
                }
                return true;

            case mwmp::RECORD_TYPE::ENCHANTMENT:
                for (const auto& record : worldstate.enchantmentRecords)
                {
                    const auto& d = record.data;
                    if (!saneRecordBase(record) || !saneCommonStrings(d)
                        || d.mData.mType < ESM::Enchantment::CastOnce || d.mData.mType > ESM::Enchantment::ConstantEffect
                        || d.mData.mCost < 0 || d.mData.mCost > 1000000
                        || d.mData.mCharge < 0 || d.mData.mCharge > kHugeInt
                        || d.mData.mFlags < 0 || (d.mData.mFlags & ~ESM::Enchantment::Autocalc) != 0
                        || !saneEffectList(d.mEffects)) return false;
                }
                return true;

            case mwmp::RECORD_TYPE::ARMOR:
                for (const auto& record : worldstate.armorRecords)
                {
                    const auto& d = record.data;
                    if (!saneRecordBase(record) || !saneCommonStrings(d) || d.mName.size() > 512 || d.mModel.size() > 1024 || d.mIcon.size() > 1024
                        || d.mEnchant.size() > 256 || d.mScript.size() > 256
                        || d.mData.mType < ESM::Armor::Helmet || d.mData.mType > ESM::Armor::RBracer
                        || !finiteNonNegative(d.mData.mWeight, 1000000.f)
                        || d.mData.mValue < 0 || d.mData.mValue > kHugeInt
                        || d.mData.mHealth < 0 || d.mData.mHealth > kHugeInt
                        || d.mData.mEnchant < 0 || d.mData.mEnchant > kHugeInt
                        || d.mData.mArmor < 0 || d.mData.mArmor > kHugeInt
                        || !sanePartList(d.mParts)) return false;
                }
                return true;

            case mwmp::RECORD_TYPE::BOOK:
                for (const auto& record : worldstate.bookRecords)
                {
                    const auto& d = record.data;
                    if (!saneRecordBase(record) || !saneCommonStrings(d) || d.mName.size() > 512 || d.mModel.size() > 1024 || d.mIcon.size() > 1024
                        || d.mText.size() > 65536 || d.mEnchant.size() > 256 || d.mScript.size() > 256
                        || !finiteNonNegative(d.mData.mWeight, 1000000.f)
                        || d.mData.mValue < 0 || d.mData.mValue > kHugeInt
                        || d.mData.mIsScroll < 0 || d.mData.mIsScroll > 1
                        || d.mData.mSkillId < -1 || d.mData.mSkillId >= 27
                        || d.mData.mEnchant < 0 || d.mData.mEnchant > kHugeInt) return false;
                }
                return true;

            case mwmp::RECORD_TYPE::CLOTHING:
                for (const auto& record : worldstate.clothingRecords)
                {
                    const auto& d = record.data;
                    if (!saneRecordBase(record) || !saneCommonStrings(d) || d.mName.size() > 512 || d.mModel.size() > 1024 || d.mIcon.size() > 1024
                        || d.mEnchant.size() > 256 || d.mScript.size() > 256
                        || d.mData.mType < ESM::Clothing::Pants || d.mData.mType > ESM::Clothing::Amulet
                        || !finiteNonNegative(d.mData.mWeight, 1000000.f)
                        || d.mData.mValue > 1000000u || d.mData.mEnchant > 1000000u
                        || !sanePartList(d.mParts)) return false;
                }
                return true;

            case mwmp::RECORD_TYPE::MISCELLANEOUS:
                for (const auto& record : worldstate.miscellaneousRecords)
                {
                    const auto& d = record.data;
                    if (!saneRecordBase(record) || !saneCommonStrings(d) || d.mName.size() > 512 || d.mModel.size() > 1024 || d.mIcon.size() > 1024
                        || d.mScript.size() > 256 || !finiteNonNegative(d.mData.mWeight, 1000000.f)
                        || d.mData.mValue < 0 || d.mData.mValue > kHugeInt
                        || d.mData.mIsKey < 0 || d.mData.mIsKey > 1) return false;
                }
                return true;

            case mwmp::RECORD_TYPE::WEAPON:
                for (const auto& record : worldstate.weaponRecords)
                {
                    const auto& d = record.data;
                    const auto& data = d.mData;
                    if (!saneRecordBase(record) || !saneCommonStrings(d) || record.quantity > 1000000u
                        || d.mName.size() > 512 || d.mModel.size() > 1024 || d.mIcon.size() > 1024
                        || d.mEnchant.size() > 256 || d.mScript.size() > 256
                        || !finiteNonNegative(data.mWeight, 1000000.f)
                        || data.mValue < 0 || data.mValue > kHugeInt
                        || data.mType < ESM::Weapon::PickProbe || data.mType > ESM::Weapon::Bolt
                        || !finiteNonNegative(data.mSpeed, 10000.f) || !finiteNonNegative(data.mReach, 10000.f)
                        || data.mHealth > 10000000u || data.mEnchant > 10000000u
                        || data.mFlags < 0 || (data.mFlags & ~(ESM::Weapon::Magical | ESM::Weapon::Silver)) != 0
                        || data.mChop[0] > data.mChop[1] || data.mSlash[0] > data.mSlash[1] || data.mThrust[0] > data.mThrust[1])
                        return false;
                }
                return true;

            default:
                // PacketRecordDynamic documents all remaining record types as
                // server-only. A client is never allowed to manufacture them.
                return false;
        }
    }
    bool validatePlayerTransition(Player& player, std::uint8_t packetId)
    {
        // Character creation legitimately initializes many values at once. The
        // server-controlled appearance flag becomes true only after login/
        // CharGen has been finalized, so delta checks begin at that boundary.
        if (!player.isAppearanceAuthoritative())
            return true;

        std::lock_guard<std::mutex> lock(sMutex);
        auto it = sStates.find(player.guid.g);
        if (it == sStates.end() || it->second.snapshotPacket != packetId)
            return true;
        const SecurityState& state = it->second;

        auto changedTooMuch = [](double before, double after, double maxDelta) {
            return !std::isfinite(before) || !std::isfinite(after) || std::abs(after - before) > maxDelta;
        };

        switch (packetId)
        {
            case ID_PLAYER_ATTRIBUTE:
                if (player.exchangeFullInfo)
                {
                    for (int i = 0; i < 8; ++i)
                        if (changedTooMuch(state.attributeBase[i], player.creatureStats.mAttributes[i].mBase, 25.0)) return false;
                }
                else
                {
                    for (std::uint8_t i : player.attributeIndexChanges)
                        if (i < 8 && changedTooMuch(state.attributeBase[i], player.creatureStats.mAttributes[i].mBase, 25.0)) return false;
                }
                break;
            case ID_PLAYER_SKILL:
                if (player.exchangeFullInfo)
                {
                    for (int i = 0; i < 27; ++i)
                        if (changedTooMuch(state.skillBase[i], player.npcStats.mSkills[i].mBase, 25.0)) return false;
                }
                else
                {
                    for (std::uint8_t i : player.skillIndexChanges)
                        if (i < 27 && changedTooMuch(state.skillBase[i], player.npcStats.mSkills[i].mBase, 25.0)) return false;
                }
                break;
            case ID_PLAYER_STATS_DYNAMIC:
                if (player.exchangeFullInfo)
                {
                    for (int i = 0; i < 3; ++i)
                        if (changedTooMuch(state.dynamicBase[i], player.creatureStats.mDynamic[i].mBase, 5000.0)) return false;
                }
                else
                {
                    for (std::uint8_t i : player.statsDynamicIndexChanges)
                        if (i < 3 && changedTooMuch(state.dynamicBase[i], player.creatureStats.mDynamic[i].mBase, 5000.0)) return false;
                }
                break;
            case ID_PLAYER_LEVEL:
                if (std::abs(player.creatureStats.mLevel - state.level) > 10) return false;
                break;
            case ID_PLAYER_BOUNTY:
                if (std::abs(static_cast<long long>(player.npcStats.mBounty) - state.bounty) > 100000LL) return false;
                break;
            case ID_PLAYER_REPUTATION:
                if (std::abs(static_cast<long long>(player.npcStats.mReputation) - state.reputation) > 1000LL) return false;
                break;
            default:
                break;
        }
        return true;
    }

}

void mwmp::CoreArenaMPSecurity::CapturePlayerState(Player& player, std::uint8_t packetId)
{
    std::lock_guard<std::mutex> lock(sMutex);
    SecurityState& state = sStates[player.guid.g];
    state.snapshotPacket = packetId;
    switch (packetId)
    {
        case ID_PLAYER_ATTRIBUTE:
            for (int i = 0; i < 8; ++i) state.attributeBase[i] = player.creatureStats.mAttributes[i].mBase;
            break;
        case ID_PLAYER_SKILL:
            for (int i = 0; i < 27; ++i) state.skillBase[i] = player.npcStats.mSkills[i].mBase;
            break;
        case ID_PLAYER_STATS_DYNAMIC:
            for (int i = 0; i < 3; ++i) state.dynamicBase[i] = player.creatureStats.mDynamic[i].mBase;
            break;
        case ID_PLAYER_LEVEL:
            state.level = player.creatureStats.mLevel;
            break;
        case ID_PLAYER_BOUNTY:
            state.bounty = player.npcStats.mBounty;
            break;
        case ID_PLAYER_REPUTATION:
            state.reputation = player.npcStats.mReputation;
            break;
        default:
            break;
    }
}

bool mwmp::CoreArenaMPSecurity::ValidatePlayerPacket(Player& player, std::uint8_t packetId)
{
    bool valid = true;
    switch (packetId)
    {
        case ID_PLAYER_POSITION:
            return ValidatePlayerPosition(player);
        case ID_PLAYER_INVENTORY:
            return ValidatePlayerInventory(player);
        case ID_CHAT_MESSAGE:
            valid = player.chatMessage.size() <= 4096;
            break;
        case ID_GUI_MESSAGEBOX:
            valid = player.guiMessageBox.id >= 0 && player.guiMessageBox.id <= 1000000
                && player.guiMessageBox.type >= BasePlayer::GUIMessageBox::MessageBox
                && player.guiMessageBox.type <= BasePlayer::GUIMessageBox::ListBox
                && player.guiMessageBox.label.size() <= 4096 && player.guiMessageBox.data.size() <= 8192;
            if (valid && player.guiMessageBox.type == BasePlayer::GUIMessageBox::CustomMessageBox)
                valid = player.guiMessageBox.buttons.size() <= 8192;
            if (valid && (player.guiMessageBox.type == BasePlayer::GUIMessageBox::InputDialog
                || player.guiMessageBox.type == BasePlayer::GUIMessageBox::PasswordDialog))
                valid = player.guiMessageBox.note.size() <= 4096;
            break;
        case ID_PLAYER_BASEINFO:
            valid = saneId(player.npc.mName, false, 256)
                && saneId(player.npc.mRace, false, 256)
                && saneId(player.npc.mModel, true, 512)
                && saneId(player.npc.mHair, true, 256)
                && saneId(player.npc.mHead, true, 256)
                && saneId(player.birthsign, true, 256)
                && (player.npc.mFlags & ~0x1Fu) == 0u
                && player.language.size() <= 8;
            break;
        case ID_PLAYER_BOOK:
            valid = player.bookChanges.size() <= 1024;
            for (const mwmp::Book& book : player.bookChanges)
                if (!saneId(book.bookId)) valid = false;
            break;
        case ID_PLAYER_TOPIC:
            valid = player.topicChanges.size() <= 1024;
            for (const mwmp::Topic& topic : player.topicChanges)
                if (!saneId(topic.topicId)) valid = false;
            break;
        case ID_PLAYER_SPEECH:
            valid = player.sound.size() <= 512;
            break;
        case ID_PLAYER_ATTRIBUTE:
            if (player.attributeIndexChanges.size() > 8) valid = false;
            for (std::uint8_t index : player.attributeIndexChanges)
                if (index >= 8) valid = false;
            for (int i = 0; valid && i < 8; ++i)
                valid = saneStatState(player.creatureStats.mAttributes[i], 10000.0, false)
                    && player.npcStats.mSkillIncrease[i] >= 0 && player.npcStats.mSkillIncrease[i] <= 1000000;
            break;
        case ID_PLAYER_SKILL:
            if (player.skillIndexChanges.size() > 27) valid = false;
            for (std::uint8_t index : player.skillIndexChanges)
                if (index >= 27) valid = false;
            for (int i = 0; valid && i < 27; ++i)
                valid = saneStatState(player.npcStats.mSkills[i], 10000.0, false);
            break;
        case ID_PLAYER_STATS_DYNAMIC:
            if (player.statsDynamicIndexChanges.size() > 3) valid = false;
            for (std::uint8_t index : player.statsDynamicIndexChanges)
                if (index >= 3) valid = false;
            for (int i = 0; valid && i < 3; ++i)
                valid = saneStatState(player.creatureStats.mDynamic[i], 10000000.0, true);
            break;
        case ID_PLAYER_LEVEL:
            valid = player.creatureStats.mLevel >= 1 && player.creatureStats.mLevel <= 10000
                && player.npcStats.mLevelProgress >= 0 && player.npcStats.mLevelProgress <= 10000000;
            break;
        case ID_PLAYER_BOUNTY:
            valid = player.npcStats.mBounty >= 0 && player.npcStats.mBounty <= kHugeInt;
            break;
        case ID_PLAYER_REPUTATION:
            valid = player.npcStats.mReputation >= 0 && player.npcStats.mReputation <= 1000000;
            break;
        case ID_PLAYER_DISPOSITION:
            valid = player.npcStats.mDisposition >= 0 && player.npcStats.mDisposition <= 100;
            break;
        case ID_PLAYER_ATTACK:
            valid = saneAttack(player.attack);
            break;
        case ID_PLAYER_CAST:
            valid = saneCast(player.cast) && (!player.cast.hasProjectile || (finitePosition(player.position) && finitePosition(player.direction)));
            break;
        case ID_PLAYER_SPELLS_ACTIVE:
            valid = saneActiveSpells(player.spellsActiveChanges);
            break;
        case ID_PLAYER_COOLDOWNS:
            valid = player.cooldownChanges.size() <= 512;
            for (const mwmp::SpellCooldown& cooldown : player.cooldownChanges)
                if (!saneId(cooldown.id) || cooldown.startTimestampDay < 0 || cooldown.startTimestampDay > 10000000
                    || !finiteNonNegative(cooldown.startTimestampHour, 24.01)) valid = false;
            break;
        case ID_PLAYER_SPELLBOOK:
            valid = player.spellbookChanges.action >= mwmp::SpellbookChanges::SET
                && player.spellbookChanges.action <= mwmp::SpellbookChanges::REMOVE
                && player.spellbookChanges.spells.size() <= 1024;
            for (const ESM::Spell& spell : player.spellbookChanges.spells)
                if (!saneId(spell.mId)) valid = false;
            break;
        case ID_PLAYER_EQUIPMENT:
            valid = player.equipmentIndexChanges.size() <= 19;
            for (int index : player.equipmentIndexChanges)
                if (index < 0 || index >= 19) valid = false;
            for (const mwmp::Item& item : player.equipmentItems)
                if (!saneInventoryItem(item, true)) valid = false;
            break;
        case ID_PLAYER_ITEM_USE:
            valid = saneInventoryItem(player.usedItem) && player.itemUseDrawState >= 0 && player.itemUseDrawState <= 4;
            break;
        case ID_PLAYER_SHAPESHIFT:
            valid = finiteNonNegative(player.scale, 100.f) && player.scale > 0.f && player.creatureRefId.size() <= 256;
            break;
        case ID_PLAYER_MISCELLANEOUS:
            valid = player.miscellaneousChangeType <= mwmp::MISCELLANEOUS_CHANGE_TYPE::SELECTED_SPELL;
            if (valid && player.miscellaneousChangeType == mwmp::MISCELLANEOUS_CHANGE_TYPE::MARK_LOCATION)
                valid = saneCell(player.markCell) && finiteVector3(player.markPosition.pos)
                    && finiteBounded(player.markPosition.rot[0], 1000000.f)
                    && finiteBounded(player.markPosition.rot[2], 1000000.f);
            if (valid && player.miscellaneousChangeType == mwmp::MISCELLANEOUS_CHANGE_TYPE::SELECTED_SPELL)
                valid = saneId(player.selectedSpellId, true);
            break;
        case ID_PLAYER_MOMENTUM:
            valid = finiteVector3(player.momentum.pos, 1000000.f);
            break;
        case ID_PLAYER_CHARGEN:
            valid = player.charGenState.currentStage >= 0 && player.charGenState.currentStage <= 64
                && player.charGenState.endStage >= 0 && player.charGenState.endStage <= 64
                && player.charGenState.currentStage <= player.charGenState.endStage;
            break;
        case ID_PLAYER_CHARCLASS:
            if (player.charClass.mId.empty())
            {
                valid = player.charClass.mName.size() <= 256 && player.charClass.mDescription.size() <= 4096
                    && player.charClass.mData.mSpecialization >= ESM::Class::Combat
                    && player.charClass.mData.mSpecialization <= ESM::Class::Stealth
                    && player.charClass.mData.mIsPlayable >= 0 && player.charClass.mData.mIsPlayable <= 1
                    && player.charClass.mData.mCalc >= 0 && (player.charClass.mData.mCalc & ~0x3FFFF) == 0;
                for (int i = 0; valid && i < 2; ++i)
                    valid = player.charClass.mData.mAttribute[i] >= 0 && player.charClass.mData.mAttribute[i] < 8;
                for (int i = 0; valid && i < 5; ++i)
                    for (int j = 0; valid && j < 2; ++j)
                        valid = player.charClass.mData.mSkills[i][j] >= 0 && player.charClass.mData.mSkills[i][j] < 27;
            }
            else valid = saneId(player.charClass.mId);
            break;
        case ID_PLAYER_FACTION:
            valid = player.factionChanges.action >= mwmp::FactionChanges::RANK
                && player.factionChanges.action <= mwmp::FactionChanges::REPUTATION
                && player.factionChanges.factions.size() <= 128;
            for (const mwmp::Faction& faction : player.factionChanges.factions)
            {
                if (!saneId(faction.factionId)) valid = false;
                if (player.factionChanges.action == mwmp::FactionChanges::RANK && (faction.rank < -1 || faction.rank > 9)) valid = false;
                if (player.factionChanges.action == mwmp::FactionChanges::REPUTATION && !boundedSigned(faction.reputation, 1000000)) valid = false;
            }
            break;
        case ID_PLAYER_JAIL:
            valid = player.jailDays >= 0 && player.jailDays <= 1000000
                && player.jailProgressText.size() <= 4096 && player.jailEndText.size() <= 4096;
            break;
        case ID_PLAYER_JOURNAL:
            valid = player.journalChanges.size() <= 1024;
            for (const mwmp::JournalItem& entry : player.journalChanges)
                if ((entry.type != mwmp::JournalItem::ENTRY && entry.type != mwmp::JournalItem::INDEX)
                    || !saneId(entry.quest) || entry.index < 0 || entry.index > 1000000
                    || (entry.type == mwmp::JournalItem::ENTRY && entry.actorRefId.size() > 256)
                    || (entry.hasTimestamp && (entry.timestamp.daysPassed < 0 || entry.timestamp.day < 1 || entry.timestamp.day > 31
                        || entry.timestamp.month < 0 || entry.timestamp.month > 11))) valid = false;
            break;
        case ID_PLAYER_QUICKKEYS:
            valid = player.quickKeyChanges.size() <= 64;
            for (const mwmp::QuickKey& key : player.quickKeyChanges)
                if (key.slot > 255 || key.type < mwmp::QuickKey::ITEM || key.type > mwmp::QuickKey::UNASSIGNED
                    || (key.type != mwmp::QuickKey::UNASSIGNED && !saneId(key.itemId))) valid = false;
            break;
        case ID_PLAYER_CELL_STATE:
        case ID_PLAYER_CELL_CHANGE:
            // Behavioral/cell-transition anti-cheat is intentionally disabled.
            // Stock TES3MP handles these packets unchanged.
            valid = true;
            break;
        case ID_PLAYER_ANIM_PLAY:
            valid = saneAnimation(player.animation);
            break;
        case ID_PLAYER_ANIM_FLAGS:
            valid = player.drawState >= 0 && player.drawState <= 4
                && (player.movementFlags & ~63u) == 0u
                && (!player.hasTcl || player.consoleAllowed);
            break;
        case ID_PLAYER_DEATH:
            valid = player.deathState >= 0 && player.deathState <= 16 && saneTarget(player.killer, true);
            break;
        case ID_PLAYER_RESURRECT:
            valid = player.resurrectType <= mwmp::RESURRECT_TYPE::TRIBUNAL_TEMPLE;
            break;
        default:
            break;
    }

    if (valid && !validatePlayerTransition(player, packetId))
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
            "CoreArenaMP security: rejected implausible numeric jump in packet %u from pid %u",
            static_cast<unsigned>(packetId), player.getId());
        return false;
    }

    if (!valid)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
            "CoreArenaMP security: rejected invalid numeric/player data packet %u from pid %u",
            static_cast<unsigned>(packetId), player.getId());
        return false;
    }

    return true;
}

bool mwmp::CoreArenaMPSecurity::ValidatePlayerPosition(Player& player)
{
    // Speed/teleport/rate behavior checks are intentionally disabled.
    // Only reject values that cannot represent a legitimate numeric state.
    if (!finitePosition(player.position) || !finitePosition(player.direction))
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
            "CoreArenaMP data guard: rejected non-finite/absurd position from pid %u", player.getId());
        return false;
    }
    return true;
}

bool mwmp::CoreArenaMPSecurity::ValidatePlayerInventory(Player& player)
{
    const auto& changes = player.inventoryChanges;
    if (changes.action < mwmp::InventoryChanges::SET
        || changes.action > mwmp::InventoryChanges::REMOVE
        || changes.items.size() > 1024)
    {
        LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
            "CoreArenaMP security: rejected malformed inventory packet from pid %u", player.getId());
        return false;
    }

    std::int64_t totalCount = 0;
    for (const mwmp::Item& item : changes.items)
    {
        if (!saneInventoryItem(item))
        {
            LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                "CoreArenaMP security: rejected invalid inventory item data from pid %u", player.getId());
            return false;
        }
    }

    return true;
}

bool mwmp::CoreArenaMPSecurity::ValidateObjectPacket(
    Player&, const BaseObjectList&, std::uint8_t)
{
    return true;
}
bool mwmp::CoreArenaMPSecurity::ValidateObjectInteraction(
    Player&, const BaseObjectList&, std::uint8_t)
{
    return true;
}
bool mwmp::CoreArenaMPSecurity::ValidateActorPacket(
    Player&, const BaseActorList&, std::uint8_t)
{
    return true;
}
bool mwmp::CoreArenaMPSecurity::ValidateWorldstatePacket(
    Player&, const BaseWorldstate&, std::uint8_t)
{
    return true;
}
void mwmp::CoreArenaMPSecurity::ResetPosition(Player&)
{
    // Behavioral movement tracking disabled.
}
void mwmp::CoreArenaMPSecurity::ForgetPlayer(Player& player)
{
    std::lock_guard<std::mutex> lock(sMutex);
    sStates.erase(player.guid.g);
}

