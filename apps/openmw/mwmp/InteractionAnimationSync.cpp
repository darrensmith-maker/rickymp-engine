#include "InteractionAnimationSync.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <exception>
#include <map>
#include <sstream>
#include <vector>

#include <components/esm/loadnpc.hpp>
#include <components/settings/settings.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwmechanics/character.hpp"
#include "../mwmechanics/animationenhancements.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwrender/animation.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/manualref.hpp"

#include "PlayerList.hpp"

namespace
{
    const char* sPrefixV1 = "arenamp_interaction|";
    const char* sPrefixV2 = "arenamp_interaction2|";
    const char* sWalkPrefix = "arenamp_walk|";
    const char* sConsumePrefix = "arenamp_consume|";
    const char* sAmbientConsumePrefix = "arenamp_ambient_consume|";
    constexpr float sDynamicInteractionTransitionSeconds = 0.16f;
    std::map<int, std::string> sWalkAnimationStyles;
    std::map<int, MWRender::PartHolderPtr> sInteractionProps;

    std::string lowerCase(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    int actorId(const MWWorld::Ptr& ptr)
    {
        if (ptr.isEmpty() || !ptr.getClass().isActor())
            return -1;
        return ptr.getClass().getCreatureStats(ptr).getActorId();
    }

    bool isPropAttached(const MWRender::PartHolderPtr& holder)
    {
        return holder && holder->getNode() && holder->getNode()->getNumParents() > 0;
    }

    std::vector<std::string> propBoneCandidates(const std::string& preferred)
    {
        std::vector<std::string> bones;
        auto appendUnique = [&bones](const std::string& name)
        {
            if (!name.empty() && std::find(bones.begin(), bones.end(), name) == bones.end())
                bones.push_back(name);
        };
        appendUnique(preferred);
        const std::string lower = lowerCase(preferred);
        const bool left = lower.find("shield") != std::string::npos
            || lower.find("left") != std::string::npos || lower.find(" l ") != std::string::npos;
        if (left)
        {
            appendUnique("Shield Bone"); appendUnique("Left Hand");
            appendUnique("Left Wrist"); appendUnique("Bip01 L Hand");
        }
        else
        {
            appendUnique("Right Hand"); appendUnique("Weapon Bone");
            appendUnique("Right Wrist"); appendUnique("Bip01 R Hand");
        }
        return bones;
    }

    MWRender::PartHolderPtr attachHandProp(MWRender::Animation* animation,
        const std::string& model, const std::string& preferredBone)
    {
        if (!animation || model.empty())
            return MWRender::PartHolderPtr();
        try
        {
            return animation->attachObjectToBone(model, propBoneCandidates(preferredBone));
        }
        catch (const std::exception&)
        {
            return MWRender::PartHolderPtr();
        }
    }

    const char* modelForProp(int prop)
    {
        switch (prop)
        {
            case 1: return "meshes\\InteractionsAnimated\\gold_025_prop.nif";
            case 2: return "meshes\\InteractionsAnimated\\text_parchment_01_prop.nif";
            case 3: return "meshes\\InteractionsAnimated\\fireball.nif";
            default: return nullptr;
        }
    }

    std::vector<std::string> splitFields(const std::string& value, std::size_t begin, std::size_t count)
    {
        std::vector<std::string> fields;
        fields.reserve(count);
        for (std::size_t i = 0; i + 1 < count; ++i)
        {
            const std::size_t end = value.find('|', begin);
            if (end == std::string::npos)
                return {};
            fields.push_back(value.substr(begin, end - begin));
            begin = end + 1;
        }
        fields.push_back(value.substr(begin));
        return fields;
    }

    bool hasMeshesPrefix(const std::string& model)
    {
        static const char prefix[] = "meshes\\";
        if (model.size() < 7)
            return false;

        for (std::size_t i = 0; i < 7; ++i)
        {
            const unsigned char left = static_cast<unsigned char>(model[i]);
            const unsigned char right = static_cast<unsigned char>(prefix[i]);
            if (std::tolower(left) != std::tolower(right))
                return false;
        }
        return true;
    }

    std::string normalizeModelPath(std::string model)
    {
        std::replace(model.begin(), model.end(), '/', '\\');
        if (model.empty())
            return model;

        if (!hasMeshesPrefix(model))
            model.insert(0, "meshes\\");

        if (model.size() > 260
            || model.find('|') != std::string::npos
            || model.find("..") != std::string::npos
            || model.find(':') != std::string::npos)
            return std::string();

        return model;
    }

    bool validModelPath(const std::string& model)
    {
        return model.empty() || !normalizeModelPath(model).empty();
    }

    std::string modelForData(const mwmp::InteractionAnimationData& data)
    {
        if (!data.propModel.empty())
            return normalizeModelPath(data.propModel);
        if (const char* fixedModel = modelForProp(data.prop))
            return fixedModel;
        return std::string();
    }

}

namespace mwmp
{
    std::string encodeInteractionAnimation(const InteractionAnimationData& data)
    {
        const std::string model = normalizeModelPath(data.propModel);
        std::ostringstream stream;
        stream << sPrefixV2 << (data.stop ? 1 : 0) << '|' << data.blendMask << '|'
            << data.speed << '|' << data.loops << '|' << data.duration << '|'
            << data.prop << '|' << model << '|' << data.group;
        return stream.str();
    }

    bool decodeInteractionAnimation(const std::string& value, InteractionAnimationData& data)
    {
        const std::string prefixV2(sPrefixV2);
        const std::string prefixV1(sPrefixV1);
        const bool version2 = value.compare(0, prefixV2.size(), prefixV2) == 0;
        const bool version1 = value.compare(0, prefixV1.size(), prefixV1) == 0;
        if (!version2 && !version1)
            return false;

        const std::size_t prefixSize = version2 ? prefixV2.size() : prefixV1.size();
        const std::vector<std::string> fields = splitFields(value, prefixSize, version2 ? 8 : 7);
        if (fields.empty())
            return false;

        try
        {
            data.stop = std::stoi(fields[0]) != 0;
            data.blendMask = std::stoi(fields[1]);
            data.speed = std::stof(fields[2]);
            data.loops = std::stoi(fields[3]);
            data.duration = std::stof(fields[4]);
            data.prop = std::stoi(fields[5]);
        }
        catch (const std::exception&)
        {
            return false;
        }

        data.propModel = version2 ? normalizeModelPath(fields[6]) : std::string();
        data.group = fields[version2 ? 7 : 6];
        if (data.group.empty() || data.group.size() > 128
            || !std::isfinite(data.speed) || !std::isfinite(data.duration)
            || !validModelPath(data.propModel))
            return false;

        data.blendMask &= MWRender::Animation::BlendMask_All;
        data.speed = std::clamp(data.speed, 0.05f, 10.f);
        data.loops = std::clamp(data.loops, 1, 100);
        data.duration = std::clamp(data.duration, 0.05f, 600.f);
        data.prop = std::clamp(data.prop, 0, 3);
        return data.blendMask != 0;
    }

    bool sameInteractionAnimation(const InteractionAnimationData& left, const InteractionAnimationData& right)
    {
        return left.group == right.group
            && left.blendMask == right.blendMask
            && std::abs(left.speed - right.speed) < 0.0001f
            && left.loops == right.loops
            && std::abs(left.duration - right.duration) < 0.0001f
            && left.prop == right.prop
            && normalizeModelPath(left.propModel) == normalizeModelPath(right.propModel)
            && left.stop == right.stop;
    }

    bool playInteractionAnimation(const MWWorld::Ptr& ptr, const InteractionAnimationData& data)
    {
        MWRender::Animation* animation = MWBase::Environment::get().getWorld()->getAnimation(ptr);
        if (!animation || data.stop || data.group.empty() || data.blendMask == 0)
            return false;

        bool animationPlaying = false;
        if (animation->hasAnimation(data.group))
        {
            // Never use Priority_Persistent for synchronized cosmetic/interaction
            // animations. In the 0.47 animation runner, the presence of any
            // Persistent state makes runAnimation() skip every non-persistent
            // state. On a DedicatedActor that freezes Walk/Run while network
            // interpolation keeps changing the actor position, producing the
            // classic "sliding with frozen legs" bug whenever ArmsFolded,
            // ArmsAtBack, gestures, etc. are active.
            //
            // Keep unselected bone groups at Default and give only the requested
            // groups ordinary non-persistent priorities. This mirrors the
            // authority-side dialogue/ambient animation path and allows lower-body
            // locomotion to continue independently under arm-only poses.
            MWRender::Animation::AnimPriority priority(MWMechanics::Priority_Default);
            if (data.blendMask & MWRender::Animation::BlendMask_LowerBody)
                priority[MWRender::Animation::BoneGroup_LowerBody] = MWMechanics::Priority_Movement;
            if (data.blendMask & MWRender::Animation::BlendMask_Torso)
                priority[MWRender::Animation::BoneGroup_Torso] = MWMechanics::Priority_Weapon;
            if (data.blendMask & MWRender::Animation::BlendMask_LeftArm)
                priority[MWRender::Animation::BoneGroup_LeftArm] = MWMechanics::Priority_Weapon;
            if (data.blendMask & MWRender::Animation::BlendMask_RightArm)
                priority[MWRender::Animation::BoneGroup_RightArm] = MWMechanics::Priority_Weapon;

            if (animation->isPlaying(data.group))
                animation->disable(data.group);
            animation->beginBoneTransition(data.blendMask, sDynamicInteractionTransitionSeconds);
            animation->play(data.group, priority, data.blendMask, true, data.speed,
                "start", "stop", 0.f, static_cast<std::size_t>(std::max(0, data.loops - 1)), true);
            animationPlaying = animation->isPlaying(data.group);
        }

        const std::string model = modelForData(data);
        const int id = actorId(ptr);
        if (!model.empty() && id >= 0)
            sInteractionProps.erase(id);

        const bool propAttached = ensureInteractionAnimationProp(ptr, data);
        return animationPlaying || propAttached;
    }

    bool ensureInteractionAnimationProp(const MWWorld::Ptr& ptr, const InteractionAnimationData& data)
    {
        MWRender::Animation* animation = MWBase::Environment::get().getWorld()->getAnimation(ptr);
        const std::string model = modelForData(data);
        const int id = actorId(ptr);
        if (!animation || data.stop || model.empty() || id < 0)
            return false;

        auto found = sInteractionProps.find(id);
        if (found != sInteractionProps.end())
        {
            if (isPropAttached(found->second))
                return true;
            sInteractionProps.erase(found);
        }

        MWRender::PartHolderPtr holder = attachHandProp(animation, model, "Right Hand");
        if (!holder)
            return false;
        sInteractionProps[id] = holder;
        return true;
    }

    void stopInteractionAnimation(const MWWorld::Ptr& ptr, const InteractionAnimationData& data)
    {
        const int id = actorId(ptr);
        if (id >= 0)
            sInteractionProps.erase(id);

        MWRender::Animation* animation = MWBase::Environment::get().getWorld()->getAnimation(ptr);
        if (!animation)
            return;
        if (!data.group.empty() && animation->isPlaying(data.group))
        {
            animation->beginBoneTransition(data.blendMask, sDynamicInteractionTransitionSeconds);
            animation->disable(data.group);
        }
    }

    std::string encodeConsumableAnimation(const std::string& refId)
    {
        if (refId.empty() || refId.size() > 128 || refId.find('|') != std::string::npos)
            return std::string();
        return std::string(sConsumePrefix) + refId;
    }

    bool decodeConsumableAnimation(const std::string& value, std::string& refId)
    {
        const std::string prefix(sConsumePrefix);
        if (value.compare(0, prefix.size(), prefix) != 0)
            return false;
        refId = value.substr(prefix.size());
        return !refId.empty() && refId.size() <= 128 && refId.find('|') == std::string::npos;
    }

    bool playConsumableAnimation(const MWWorld::Ptr& ptr, const std::string& refId)
    {
        if (ptr.isEmpty() || refId.empty())
            return false;
        try
        {
            MWWorld::ManualRef item(MWBase::Environment::get().getWorld()->getStore(), refId, 1);
            ArenaMW::notifyConsumableUsed(ptr, item.getPtr());
            return ArenaMW::isConsumingAnimationActive(ptr);
        }
        catch (const std::exception&)
        {
            return false;
        }
    }

    std::string encodeAmbientConsumableAnimation(const std::string& refId)
    {
        if (refId.empty() || refId.size() > 128 || refId.find('|') != std::string::npos)
            return std::string();
        return std::string(sAmbientConsumePrefix) + refId;
    }

    bool decodeAmbientConsumableAnimation(const std::string& value, std::string& refId)
    {
        const std::string prefix(sAmbientConsumePrefix);
        if (value.compare(0, prefix.size(), prefix) != 0)
            return false;
        refId = value.substr(prefix.size());
        return !refId.empty() && refId.size() <= 128 && refId.find('|') == std::string::npos;
    }

    bool playAmbientConsumableAnimation(const MWWorld::Ptr& ptr, const std::string& refId)
    {
        return ArenaMW::playAmbientConsumableAnimation(ptr, refId);
    }

    bool isValidWalkAnimationStyle(const std::string& group)
    {
        return group.empty()
            || group == "walkforward_dirn154"
            || group == "walkforward_march154"
            || group == "walkforward_mw"
            || group == "walkforward_mwfem";
    }

    std::string encodeWalkAnimationStyle(const std::string& group)
    {
        return std::string(sWalkPrefix) + (isValidWalkAnimationStyle(group) ? group : std::string());
    }

    bool decodeWalkAnimationStyle(const std::string& value, std::string& group)
    {
        const std::string prefix(sWalkPrefix);
        if (value.compare(0, prefix.size(), prefix) != 0)
            return false;

        group = value.substr(prefix.size());
        if (group.size() > 64 || !isValidWalkAnimationStyle(group))
            return false;
        return true;
    }

    void setWalkAnimationStyle(const MWWorld::Ptr& ptr, const std::string& group)
    {
        const int id = actorId(ptr);
        if (id < 0 || !isValidWalkAnimationStyle(group))
            return;

        const auto current = sWalkAnimationStyles.find(id);
        if ((current == sWalkAnimationStyles.end() && group.empty())
            || (current != sWalkAnimationStyles.end() && current->second == group))
            return;

        if (group.empty())
            sWalkAnimationStyles.erase(id);
        else
            sWalkAnimationStyles[id] = group;

        if (MWBase::MechanicsManager* mechanics
            = MWBase::Environment::get().getMechanicsManager())
            mechanics->forceStateUpdate(ptr);
    }

    void clearWalkAnimationStyle(const MWWorld::Ptr& ptr)
    {
        const int id = actorId(ptr);
        if (id >= 0)
            sWalkAnimationStyles.erase(id);
    }

    std::string getWalkAnimationStyle(const MWWorld::Ptr& ptr)
    {
        const int id = actorId(ptr);
        const auto found = sWalkAnimationStyles.find(id);
        return found != sWalkAnimationStyles.end() ? found->second : std::string();
    }

    std::string getDynamicMovementAnimation(const MWWorld::Ptr& ptr,
        const std::string& baseGroup)
    {
        if (ptr.isEmpty() || !ptr.getClass().isNpc())
            return std::string();

        MWBase::World* world = MWBase::Environment::get().getWorld();
        const bool localPlayer = ptr == world->getPlayerPtr();
        const bool dedicatedPlayer = PlayerList::isDedicatedPlayer(ptr);

        // Dynamic Animations supplies separate lower-body locomotion sources
        // for the first-person player armature. Remote players and NPCs are
        // always rendered with their third-person armature.
        if (localPlayer && world->isFirstPerson()
            && Settings::Manager::getBool("dynamic first person locomotion", "GUI"))
        {
            static const std::map<std::string, std::string> sFirstPersonGroups = {
                { "walkforward", "WalkForward_base" },
                { "walkback", "WalkBack_base" },
                { "walkleft", "WalkLeft_base" },
                { "walkright", "WalkRight_base" },
                { "runforward", "RunForward_base" },
                { "runback", "RunBack_base" },
                { "runleft", "RunLeft_base" },
                { "runright", "RunRight_base" },
                { "sneakforward", "SneakForward_base" },
                { "sneakback", "SneakBack_base" },
                { "sneakleft", "SneakLeft_base" },
                { "sneakright", "SneakRight_base" },
            };
            const auto found = sFirstPersonGroups.find(baseGroup);
            return found != sFirstPersonGroups.end() ? found->second : std::string();
        }

        // Many players use Always Run, so selecting a walking style while the
        // controller is in RunForward previously appeared to do nothing. The
        // custom cycle is speed-adjusted by CharacterController, making it safe
        // to use for both forward walking and forward running.
        if (localPlayer || dedicatedPlayer)
        {
            if (baseGroup == "walkforward" || baseGroup == "runforward")
            {
                // The network token is intentionally lowercase, but KF text-key
                // group matching is case-sensitive. Resolve it to the real group
                // names used by the bundled Dynamic Animations files.
                const std::string style = getWalkAnimationStyle(ptr);
                if (style == "walkforward_dirn154")
                    return "WalkForward_dirn154";
                if (style == "walkforward_march154")
                    return "WalkForward_march154";
                if (style == "walkforward_mw")
                    return "WalkForward_mw";
                if (style == "walkforward_mwfem")
                    return "WalkForward_mwFem";
            }
            return std::string();
        }

        if (baseGroup != "walkforward")
            return std::string();

        if (!Settings::Manager::getBool("dynamic actor locomotion", "GUI"))
            return std::string();

        const ESM::NPC* npc = ptr.get<ESM::NPC>()->mBase;
        if (!npc)
            return std::string();

        const std::string id = lowerCase(npc->mId);
        const std::string name = lowerCase(npc->mName);
        const std::string npcClass = lowerCase(npc->mClass);
        const std::string race = lowerCase(npc->mRace);
        std::string model = lowerCase(npc->mModel);
        std::replace(model.begin(), model.end(), '\\', '/');
        const bool supportedCustomModel = model.empty()
            || model == "meshes/base_animkna.nif"
            || model == "base_animkna.nif"
            || model == "meshes/epos_kha_upr_anim_f.nif"
            || model == "epos_kha_upr_anim_f.nif"
            || model == "meshes/epos_kha_upr_anim_m.nif"
            || model == "epos_kha_upr_anim_m.nif";
        if (!supportedCustomModel)
            return std::string();

        const bool guard = npcClass.find("guard") != std::string::npos
            || npcClass.find("crusader") != std::string::npos
            || npcClass.find("master-at-arms") != std::string::npos
            || id.find("ordinator") != std::string::npos
            || name.find("ordinator") != std::string::npos;
        const bool beast = race == "argonian" || race == "khajiit";

        if (guard)
            return "WalkForward_march";
        if (beast)
            return "WalkForward_spd09";
        if (!npc->isMale()
            && (npcClass.find("noble") != std::string::npos
                || npcClass.find("merchant") != std::string::npos
                || race == "high elf"))
            return "WalkForward_noble";
        if (!npc->isMale())
            return "WalkForward_spd09";
        return "WalkForward_mw";
    }
}
