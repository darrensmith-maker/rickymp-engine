#include "animationenhancements.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include <osg/Matrix>
#include <osg/Node>
#include <osg/Transform>

#include <components/esm/loadalch.hpp>
#include <components/esm/loadingr.hpp>
#include <components/esm/loadnpc.hpp>
#include <components/settings/settings.hpp>
#include <components/misc/rng.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwmechanics/character.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/movement.hpp"
#include "../mwrender/animation.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/interactionanimation.hpp"
#include "../mwmp/PlayerList.hpp"
#include "../mwmp/Main.hpp"
#include "../mwmp/CellController.hpp"
#include "../mwmp/LocalActor.hpp"
#include "../mwmp/InteractionAnimationSync.hpp"



namespace
{
    constexpr float sDynamicPoseTransitionSeconds = 0.18f;
    constexpr float sDynamicInteractionTransitionSeconds = 0.16f;
    constexpr float sConsumingTransitionSeconds = 0.16f;
    std::map<int, std::string> sWalkAnimationStyles;

    struct PersistentAnimationState
    {
        std::string mGroup;
        int mRequestedMask = 0;
        int mAppliedMask = 0;
        float mSpeed = 1.f;
        bool mPlaying = false;
    };

    std::map<int, PersistentAnimationState> sPersistentAnimations;

    struct ConsumableAnimationSpec
    {
        std::string mGroup;
        std::string mPropModel;
        std::string mPropBone = "Weapon Bone";
        std::string mSoundFile;
        float mFallbackDuration = 2.f;
        float mPropRemoveTime = -1.f;
        float mSoundTime = -1.f;
        bool mSoundAtEnd = false;
        bool mShatterProp = false;
    };

    struct ConsumingAnimationState
    {
        MWWorld::Ptr mActor;
        std::string mGroup;
        std::string mPropModel;
        std::string mSoundFile;
        int mBlendMask = 0;
        float mElapsed = 0.f;
        float mDuration = 2.f;
        float mPropRemoveTime = -1.f;
        float mSoundTime = -1.f;
        bool mSoundAtEnd = false;
        bool mPropRemoved = false;
        bool mSoundPlayed = false;
        bool mWeaponHidden = false;
        bool mShatterProp = false;
        bool mShatterPlayed = false;
        bool mAmbient = false;
        std::string mPropBone;
        MWRender::PartHolderPtr mPropHolder;
    };

    std::map<int, ConsumingAnimationState> sConsumingAnimations;
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
            || lower.find("left") != std::string::npos
            || lower.find(" l ") != std::string::npos;
        if (left)
        {
            appendUnique("Shield Bone");
            appendUnique("Left Hand");
            appendUnique("Left Wrist");
            appendUnique("Bip01 L Hand");
        }
        else
        {
            appendUnique("Right Hand");
            appendUnique("Weapon Bone");
            appendUnique("Right Wrist");
            appendUnique("Bip01 R Hand");
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

    std::string modelForData(const ArenaMW::InteractionAnimationData& data)
    {
        if (!data.propModel.empty())
            return normalizeModelPath(data.propModel);
        if (const char* fixedModel = modelForProp(data.prop))
            return fixedModel;
        return std::string();
    }

    bool hasInventoryItem(const MWWorld::Ptr& actor, const std::set<std::string>& ids)
    {
        if (actor.isEmpty() || !actor.getClass().hasInventoryStore(actor))
            return false;

        MWWorld::InventoryStore& store = actor.getClass().getInventoryStore(actor);
        for (const std::string& id : ids)
        {
            if (!store.search(id).isEmpty())
                return true;
        }
        return false;
    }

    ConsumableAnimationSpec resolveConsumable(const MWWorld::Ptr& actor, const MWWorld::Ptr& item)
    {
        ConsumableAnimationSpec spec;
        if (item.isEmpty())
            return spec;

        const std::string id = lowerCase(item.getCellRef().getRefId());
        const bool ingredient = item.getTypeName() == typeid(ESM::Ingredient).name();
        const bool potion = item.getTypeName() == typeid(ESM::Potion).name();
        if (!ingredient && !potion)
            return spec;

        // Keep the original package's explicit blacklist for non-consumable
        // potion-like records and its pre-rolled smoke variants.
        if (id.find("s3_soapinv_") == 0 || id.find("pe_hackle-lo_smoke") == 0)
            return spec;

        static const std::set<std::string> skoomaPipes = {
            "apparatus_a_spipe_01", "apparatus_a_spipe_tsiya"
        };
        static const std::set<std::string> smokePipes = {
            "t_de_hacklopipe_01", "t_imp_nibpipebamb_01", "t_we_bonewarepipe_01",
            "t_imp_colpipecob_01", "t_com_pipe_01", "t_com_pipe_02", "mc_pipe",
            "hackle-lo pipe - wood", "hackle-lo pipe - windwalker", "hackle-lo pipe - stalhrim",
            "hackle-lo pipe - silver", "hackle-lo pipe - peace", "hackle-lo pipe - mushroom"
        };

        if (id == "ingred_moon_sugar_01")
        {
            if (hasInventoryItem(actor, skoomaPipes))
            {
                spec.mGroup = "SkoomaPipe";
                spec.mPropModel = "meshes\\consan\\spipe_vfx.nif";
                spec.mPropBone = "Shield Bone";
                spec.mSoundFile = "Sound/detd_Bongsound.wav";
                spec.mFallbackDuration = 3.f;
                spec.mPropRemoveTime = 3.f;
                spec.mSoundTime = .7f;
                return spec;
            }
        }

        if (id == "ingred_hackle-lo_leaf_01" || id == "t_ingflor_tanna_01")
        {
            if (hasInventoryItem(actor, smokePipes))
            {
                spec.mGroup = "smokepipe1";
                spec.mPropModel = "meshes\\consan\\d_pipe_cob.nif";
                spec.mSoundFile = "Sound/merged_smoke.wav";
                spec.mFallbackDuration = 7.f;
                spec.mPropRemoveTime = 7.f;
                spec.mSoundTime = 2.8f;
                return spec;
            }
        }

        if (ingredient)
        {
            spec.mGroup = "eatingr";
            spec.mPropModel = normalizeModelPath(item.getClass().getModel(item));
            spec.mSoundFile = "Sound/freesound_community-eating-sound-effect-36186.mp3";
            spec.mFallbackDuration = 2.f;
            spec.mPropRemoveTime = (id == "food_kwama_egg_01" || id == "food_kwama_egg_02"
                || id == "ingred_coprinus_01" || id == "ingred_russula_01") ? .9f : 1.6f;
            spec.mSoundTime = .7f;
            return spec;
        }

        spec.mGroup = "potion";
        spec.mPropModel = "meshes\\consan\\potion_standard_drink.nif";
        // Bottle destruction is handled by the native LuaPhysics-style shatter
        // event at the animation discard key rather than by a detached MP3.
        spec.mSoundFile.clear();
        spec.mFallbackDuration = 2.f;
        spec.mSoundAtEnd = false;
        spec.mShatterProp = true;

        if (id == "potion_t_bug_musk_01")
            spec.mGroup = "bugmusk2";

        static const std::map<std::string, std::string> drinkMeshes = {
            { "potion_comberry_brandy_01", "meshes\\consan\\Comberry_brandy_01.nif" },
            { "potion_comberry_wine_01", "meshes\\consan\\Comberry_wine_01.nif" },
            { "potion_cyro_brandy_01", "meshes\\consan\\Cyro_brandy_01.nif" },
            { "potion_cyro_whiskey_01", "meshes\\consan\\Cyro_whiskey_01.nif" },
            { "potion_local_brew_01", "meshes\\consan\\Local_brew_01.nif" },
            { "potion_local_liquor_01", "meshes\\consan\\Local_liquor_01.nif" },
            { "p_vintagecomberrybrandy1", "meshes\\consan\\Comberry_brandy_01.nif" },
            { "potion_skooma_01", "meshes\\consan\\Skooma_01.nif" }
        };
        const auto drink = drinkMeshes.find(id);
        if (drink != drinkMeshes.end())
        {
            spec.mGroup = "drinkbone";
            spec.mPropModel = drink->second;
        }
        else if (id == "potion_ancient_brandy" || id == "potion_nord_mead")
        {
            spec.mGroup = "drinkbone";
            const std::string ownModel = normalizeModelPath(item.getClass().getModel(item));
            spec.mPropModel = ownModel.empty() ? "meshes\\consan\\o_goblet_juicr.nif" : ownModel;
        }
        else
        {
            // Native compatibility for the large MOD_SPECIFIC_ANIM table from
            // the Lua version without hard-coding hundreds of plugin records.
            // Most external beverage records advertise their purpose in the ID.
            static const std::vector<std::string> drinkIdTokens = {
                "drink", "wine", "beer", "brandy", "whiskey", "liquor",
                "mead", "brew", "cider", "juice", "tea", "milk", "rum"
            };
            bool looksLikeDrink = false;
            for (const std::string& token : drinkIdTokens)
            {
                if (id.find(token) != std::string::npos)
                {
                    looksLikeDrink = true;
                    break;
                }
            }

            const ESM::Potion* record = item.get<ESM::Potion>()->mBase;
            const bool sunsDuskDrink = record
                && lowerCase(record->mScript) == "sd_liquid_tracker";
            if (looksLikeDrink || sunsDuskDrink)
            {
                spec.mGroup = "drinkbone";
                const std::string ownModel = normalizeModelPath(item.getClass().getModel(item));
                spec.mPropModel = ownModel.empty() ? "meshes\\consan\\o_goblet_juicr.nif" : ownModel;
                // Sun's Dusk keeps its bottles intact, matching the original mod.
                if (sunsDuskDrink)
                {
                    spec.mSoundFile.clear();
                    spec.mSoundAtEnd = false;
                    spec.mShatterProp = false;
                }
            }
        }

        return spec;
    }

    osg::Vec3f handPropWorldPosition(const ConsumingAnimationState& state)
    {
        const osg::Vec3f fallback = state.mActor.isEmpty()
            ? osg::Vec3f() : state.mActor.getRefData().getPosition().asVec3();
        if (!state.mPropHolder || !state.mPropHolder->getNode())
            return fallback;

        const osg::NodePathList paths = state.mPropHolder->getNode()->getParentalNodePaths();
        if (paths.empty())
            return fallback;
        return osg::computeLocalToWorld(paths.front()).getTrans();
    }

    void shatterConsumableProp(ConsumingAnimationState& state, const osg::Vec3f& position)
    {
        if (!state.mShatterProp || state.mShatterPlayed || state.mActor.isEmpty()
            || !Settings::Manager::getBool("animated consuming bottle shatter", "GUI"))
            return;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        world->spawnEffect("meshes\\e\\physics\\transparent_liquid_shatter.nif",
            std::string(), position, 1.f, false);

        MWBase::SoundManager* sound = MWBase::Environment::get().getSoundManager();
        const float pitch = 0.8f + static_cast<float>(Misc::Rng::rollDice(201)) / 1000.f;
        sound->playSoundFile3D(state.mActor, "sound/physics/Glass_Crash_Wet__01.wav", 1.f, pitch);
        sound->playSoundFile3D(state.mActor, "sound/physics/extra/liquid_spill.wav", .65f, pitch);
        state.mShatterPlayed = true;
    }

    void playConsumableSound(ConsumingAnimationState& state)
    {
        if (state.mSoundPlayed || state.mSoundFile.empty() || state.mActor.isEmpty())
            return;
        MWBase::Environment::get().getSoundManager()->playSoundFile3D(
            state.mActor, state.mSoundFile, .75f, 1.f);
        state.mSoundPlayed = true;
    }

    void removeConsumableProp(ConsumingAnimationState& state, MWRender::Animation*)
    {
        if (!state.mPropRemoved)
        {
            const osg::Vec3f propPosition = handPropWorldPosition(state);
            state.mPropHolder.reset();
            state.mPropRemoved = true;
            shatterConsumableProp(state, propPosition);
        }
    }

    void finishConsumingAnimation(ConsumingAnimationState& state, MWRender::Animation* animation,
        bool playEndSound)
    {
        if (playEndSound && state.mSoundAtEnd)
            playConsumableSound(state);

        removeConsumableProp(state, animation);
        if (animation && animation->isPlaying(state.mGroup))
        {
            animation->beginBoneTransition(state.mBlendMask, sConsumingTransitionSeconds);
            animation->disable(state.mGroup);
        }

        if (animation && state.mWeaponHidden && !state.mActor.isEmpty()
            && state.mActor.getClass().isActor())
        {
            const bool shouldShow = !state.mActor.getClass().getCreatureStats(state.mActor).isDead()
                && state.mActor.getClass().getCreatureStats(state.mActor).getDrawState()
                    != MWMechanics::DrawState_Nothing;
            animation->showWeapons(shouldShow);
        }
    }


    bool startConsumingAnimation(const MWWorld::Ptr& actor, ConsumableAnimationSpec spec, bool ambient)
    {
        if (actor.isEmpty() || spec.mGroup.empty() || !actor.getClass().isActor())
            return false;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        const bool localPlayer = actor == world->getPlayerPtr();
        const bool player = localPlayer || mwmp::PlayerList::isDedicatedPlayer(actor);
        const MWMechanics::CreatureStats& stats = actor.getClass().getCreatureStats(actor);
        if (stats.isDead())
            return false;

        const int id = actorId(actor);
        if (id < 0 || sConsumingAnimations.find(id) != sConsumingAnimations.end())
            return false;

        MWRender::Animation* animation = world->getAnimation(actor);
        if (!animation || !animation->hasAnimation(spec.mGroup))
            return false;

        if (localPlayer && MWWorld::InteractionAnimation::isActive())
            MWWorld::InteractionAnimation::cancel();

        // Ambient habits are visual behaviour only. They must never create the
        // bottle-shatter effect that represents a genuinely consumed potion.
        if (ambient)
        {
            spec.mShatterProp = false;
            spec.mSoundAtEnd = false;
        }

        ConsumingAnimationState state;
        state.mActor = actor;
        state.mGroup = spec.mGroup;
        state.mPropModel = spec.mPropModel;
        state.mPropBone = spec.mPropBone;
        state.mSoundFile = spec.mSoundFile;
        state.mBlendMask = MWRender::Animation::BlendMask_Torso
            | MWRender::Animation::BlendMask_LeftArm
            | MWRender::Animation::BlendMask_RightArm;
        state.mPropRemoveTime = spec.mPropRemoveTime;
        state.mSoundTime = spec.mSoundTime;
        state.mSoundAtEnd = spec.mSoundAtEnd;
        state.mShatterProp = spec.mShatterProp;
        state.mAmbient = ambient;

        const float speed = ambient
            ? std::max(.25f, Settings::Manager::getFloat("dynamic ambient habit speed", "GUI"))
            : (player
                ? std::max(.25f, Settings::Manager::getFloat("animated consuming speed", "GUI"))
                : std::max(.25f, Settings::Manager::getFloat("animated consuming npc speed", "GUI")));

        MWRender::Animation::AnimPriority priority(MWMechanics::Priority_Weapon);
        priority[MWRender::Animation::BoneGroup_Torso] = MWMechanics::Priority_Persistent;
        priority[MWRender::Animation::BoneGroup_LeftArm] = MWMechanics::Priority_Persistent;
        priority[MWRender::Animation::BoneGroup_RightArm] = MWMechanics::Priority_Persistent;

        animation->beginBoneTransition(state.mBlendMask, sConsumingTransitionSeconds);
        animation->play(spec.mGroup, priority, state.mBlendMask, false, speed,
            "start", "stop", 0.f, 0, true);
        if (!animation->isPlaying(spec.mGroup))
            return false;

        const float start = animation->getStartTime(spec.mGroup);
        const float stop = animation->getTextKeyTime(spec.mGroup + ": stop");
        if (start >= 0.f && stop > start)
            state.mDuration = (stop - start) / speed;
        else
            state.mDuration = spec.mFallbackDuration / speed;

        // A KF discard key is preferred because it matches the actual hand motion.
        // For food/special props the package uses explicit timing instead.
        if (state.mPropRemoveTime < 0.f)
        {
            const float discard = animation->getTextKeyTime(spec.mGroup + ": discard");
            if (discard >= start && start >= 0.f)
                state.mPropRemoveTime = (discard - start) / speed;
        }
        else
            state.mPropRemoveTime /= speed;
        if (state.mSoundTime >= 0.f)
            state.mSoundTime /= speed;

        if (stats.getDrawState() != MWMechanics::DrawState_Nothing)
        {
            animation->showWeapons(false);
            state.mWeaponHidden = true;
        }

        if (!state.mPropModel.empty())
        {
            state.mPropHolder = attachHandProp(animation, state.mPropModel, state.mPropBone);
            state.mPropRemoved = false;
        }
        else
            state.mPropRemoved = true;

        sConsumingAnimations.emplace(id, std::move(state));
        return true;
    }

    constexpr const char* sAmbientGenericDrinkToken = "__arenamp_generic_drink__";

    void queueNpcAnimPlay(const MWWorld::Ptr& actor, const std::string& encoded)
    {
        if (actor.isEmpty() || encoded.empty())
            return;

        mwmp::CellController* cellController = mwmp::Main::get().getCellController();
        if (!cellController || !cellController->isLocalActor(actor))
            return;

        if (mwmp::LocalActor* localActor = cellController->getLocalActor(actor))
        {
            localActor->animation.groupname = encoded;
            localActor->animation.mode = 0;
            localActor->animation.count = 1;
            localActor->animation.persist = false;
        }
    }

    int getPlayerPoseBlendMask(const MWWorld::Ptr& ptr, int requestedMask)
    {
        int result = requestedMask;
        if (!ptr.isEmpty() && ptr.getClass().hasInventoryStore(ptr))
        {
            MWWorld::InventoryStore& store = ptr.getClass().getInventoryStore(ptr);
            MWWorld::ContainerStoreIterator carried
                = store.getSlot(MWWorld::InventoryStore::Slot_CarriedLeft);
            if (carried != store.end()
                && (carried.getType() == MWWorld::ContainerStore::Type_Armor
                    || carried.getType() == MWWorld::ContainerStore::Type_Light))
            {
                result &= ~MWRender::Animation::BlendMask_LeftArm;
            }
        }
        return result;
    }

    bool playerPoseIsBlocked(const MWWorld::Ptr& ptr)
    {
        if (ptr.isEmpty() || !ptr.isInCell())
            return true;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        const MWMechanics::CreatureStats& stats = ptr.getClass().getCreatureStats(ptr);
        const MWMechanics::Movement& movement = ptr.getClass().getMovementSettings(ptr);
        const bool moving = std::abs(movement.mPosition[0]) > 0.05f
            || std::abs(movement.mPosition[1]) > 0.05f
            || std::abs(movement.mPosition[2]) > 0.05f;
        const bool jumping = !world->isOnGround(ptr)
            && !world->isSwimming(ptr) && !world->isFlying(ptr);

        return jumping || moving || stats.isDead() || stats.getKnockedDown()
            || !ptr.getRefData().getAnimationState().mScriptedAnims.empty()
            || stats.getAiSequence().isInCombat()
            || stats.getDrawState() != MWMechanics::DrawState_Nothing
            || world->isSwimming(ptr)
            || MWWorld::InteractionAnimation::isActive()
            || ArenaMW::isConsumingAnimationActive(ptr);
    }

    bool playPlayerPose(const MWWorld::Ptr& ptr, const std::string& group,
        int blendMask, float speed)
    {
        MWRender::Animation* animation = MWBase::Environment::get().getWorld()->getAnimation(ptr);
        if (!animation || group.empty() || blendMask == 0 || !animation->hasAnimation(group))
            return false;

        MWRender::Animation::AnimPriority priority(MWMechanics::Priority_Default);
        if (blendMask & MWRender::Animation::BlendMask_LowerBody)
            priority[MWRender::Animation::BoneGroup_LowerBody] = MWMechanics::Priority_Weapon;
        if (blendMask & MWRender::Animation::BlendMask_Torso)
            priority[MWRender::Animation::BoneGroup_Torso] = MWMechanics::Priority_Weapon;
        if (blendMask & MWRender::Animation::BlendMask_LeftArm)
            priority[MWRender::Animation::BoneGroup_LeftArm] = MWMechanics::Priority_Weapon;
        if (blendMask & MWRender::Animation::BlendMask_RightArm)
            priority[MWRender::Animation::BoneGroup_RightArm] = MWMechanics::Priority_Weapon;

        if (animation->isPlaying(group))
            animation->disable(group);
        animation->beginBoneTransition(blendMask, sDynamicPoseTransitionSeconds);
        animation->play(group, priority, blendMask, false, speed,
            "start", "stop", 0.f, 1000000, true);
        return animation->isPlaying(group);
    }

    void stopPlayerPose(const MWWorld::Ptr& ptr, const std::string& group, int blendMask)
    {
        if (group.empty())
            return;
        if (MWRender::Animation* animation
            = MWBase::Environment::get().getWorld()->getAnimation(ptr))
        {
            if (animation->isPlaying(group))
            {
                animation->beginBoneTransition(blendMask, sDynamicPoseTransitionSeconds);
                animation->disable(group);
            }
        }
    }
}

namespace ArenaMW
{
    bool playInteractionAnimation(const MWWorld::Ptr& ptr, const InteractionAnimationData& data)
    {
        MWRender::Animation* animation = MWBase::Environment::get().getWorld()->getAnimation(ptr);
        if (!animation || data.group.empty() || data.blendMask == 0)
            return false;

        bool animationPlaying = false;
        if (animation->hasAnimation(data.group))
        {
            MWRender::Animation::AnimPriority priority(MWMechanics::Priority_Weapon);
            if (data.blendMask & MWRender::Animation::BlendMask_LowerBody)
                priority[MWRender::Animation::BoneGroup_LowerBody] = MWMechanics::Priority_Persistent;
            if (data.blendMask & MWRender::Animation::BlendMask_Torso)
                priority[MWRender::Animation::BoneGroup_Torso] = MWMechanics::Priority_Persistent;
            if (data.blendMask & MWRender::Animation::BlendMask_LeftArm)
                priority[MWRender::Animation::BoneGroup_LeftArm] = MWMechanics::Priority_Persistent;
            if (data.blendMask & MWRender::Animation::BlendMask_RightArm)
                priority[MWRender::Animation::BoneGroup_RightArm] = MWMechanics::Priority_Persistent;

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
        if (!animation || model.empty() || id < 0)
            return false;

        auto found = sInteractionProps.find(id);
        if (found != sInteractionProps.end())
        {
            if (isPropAttached(found->second))
                return true;
            // A first/third-person or equipment rebuild can detach the old
            // subtree. Drop that stale holder and recreate it on the new rig.
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

    bool isValidWalkAnimationStyle(const std::string& group)
    {
        return group.empty()
            || group == "walkforward_dirn154"
            || group == "walkforward_march154"
            || group == "walkforward_mw"
            || group == "walkforward_mwfem";
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

    void setPersistentAnimation(const MWWorld::Ptr& ptr, const std::string& group,
        int blendMask, float speed)
    {
        const int id = actorId(ptr);
        if (id < 0)
            return;

        PersistentAnimationState& state = sPersistentAnimations[id];
        const int previousMask = state.mAppliedMask != 0 ? state.mAppliedMask : state.mRequestedMask;
        stopPlayerPose(ptr, state.mGroup, previousMask);
        state.mGroup = group;
        state.mRequestedMask = blendMask;
        state.mAppliedMask = 0;
        state.mSpeed = speed;
        state.mPlaying = false;

        if (group.empty())
            sPersistentAnimations.erase(id);
        else
            updatePersistentAnimation(ptr, 0.f);
    }

    void clearPersistentAnimation(const MWWorld::Ptr& ptr)
    {
        const int id = actorId(ptr);
        const auto found = sPersistentAnimations.find(id);
        if (found == sPersistentAnimations.end())
            return;

        const int previousMask = found->second.mAppliedMask != 0
            ? found->second.mAppliedMask : found->second.mRequestedMask;
        stopPlayerPose(ptr, found->second.mGroup, previousMask);
        sPersistentAnimations.erase(found);
    }

    void updatePersistentAnimation(const MWWorld::Ptr& ptr, float)
    {
        const int id = actorId(ptr);
        const auto found = sPersistentAnimations.find(id);
        if (found == sPersistentAnimations.end())
            return;

        PersistentAnimationState& state = found->second;
        MWRender::Animation* renderer = MWBase::Environment::get().getWorld()->getAnimation(ptr);
        if (!renderer)
            return;

        if (playerPoseIsBlocked(ptr))
        {
            if (state.mPlaying || renderer->isPlaying(state.mGroup))
            {
                const int previousMask = state.mAppliedMask != 0
                    ? state.mAppliedMask : state.mRequestedMask;
                renderer->beginBoneTransition(previousMask, sDynamicPoseTransitionSeconds);
                renderer->disable(state.mGroup);
            }
            state.mPlaying = false;
            state.mAppliedMask = 0;
            return;
        }

        const int effectiveMask = getPlayerPoseBlendMask(ptr, state.mRequestedMask);
        if (effectiveMask == 0)
        {
            if (renderer->isPlaying(state.mGroup))
            {
                const int previousMask = state.mAppliedMask != 0
                    ? state.mAppliedMask : state.mRequestedMask;
                renderer->beginBoneTransition(previousMask, sDynamicPoseTransitionSeconds);
                renderer->disable(state.mGroup);
            }
            state.mPlaying = false;
            state.mAppliedMask = 0;
            return;
        }

        if (effectiveMask != state.mAppliedMask
            || !state.mPlaying || !renderer->isPlaying(state.mGroup))
        {
            if (renderer->isPlaying(state.mGroup))
                renderer->disable(state.mGroup);
            state.mPlaying = playPlayerPose(ptr, state.mGroup, effectiveMask, state.mSpeed);
            state.mAppliedMask = state.mPlaying ? effectiveMask : 0;
        }
    }

    void notifyConsumableUsed(const MWWorld::Ptr& actor, const MWWorld::Ptr& item)
    {
        if (actor.isEmpty() || item.isEmpty() || !actor.getClass().isActor())
            return;
        if (!Settings::Manager::getBool("animated consuming", "GUI"))
            return;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        const bool player = actor == world->getPlayerPtr() || mwmp::PlayerList::isDedicatedPlayer(actor);
        if (!player && !Settings::Manager::getBool("animated consuming npc", "GUI"))
            return;

        ConsumableAnimationSpec spec = resolveConsumable(actor, item);
        if (!spec.mGroup.empty() && startConsumingAnimation(actor, std::move(spec), false) && !player)
            queueNpcAnimPlay(actor, mwmp::encodeConsumableAnimation(item.getCellRef().getRefId()));
    }

    bool tryStartAmbientNpcHabit(const MWWorld::Ptr& actor)
    {
        if (actor.isEmpty() || !actor.isInCell() || !actor.getClass().isNpc())
            return false;
        if (!Settings::Manager::getBool("animated consuming", "GUI")
            || !Settings::Manager::getBool("animated consuming npc", "GUI")
            || !Settings::Manager::getBool("dynamic ambient habits", "GUI"))
            return false;

        MWBase::World* world = MWBase::Environment::get().getWorld();
        if (actor == world->getPlayerPtr() || mwmp::PlayerList::isDedicatedPlayer(actor)
            || isConsumingAnimationActive(actor))
            return false;

        const MWMechanics::CreatureStats& stats = actor.getClass().getCreatureStats(actor);
        const MWMechanics::Movement& movement = actor.getClass().getMovementSettings(actor);
        if (stats.isDead() || stats.getKnockedDown() || stats.getAiSequence().isInCombat()
            || stats.getDrawState() != MWMechanics::DrawState_Nothing
            || std::abs(movement.mPosition[0]) > .05f
            || std::abs(movement.mPosition[1]) > .05f
            || std::abs(movement.mPosition[2]) > .05f
            || world->isSwimming(actor))
            return false;

        MWRender::Animation* animation = world->getAnimation(actor);
        if (!animation || !animation->upperBodyReady())
            return false;

        // Prefer something the NPC actually carries.  This gives food, potions,
        // alcohol, Moon Sugar + skooma pipes and Hackle-Lo + smoking pipes the same
        // visual treatment as genuine consumption, without changing inventory count.
        std::vector<MWWorld::Ptr> candidates;
        if (actor.getClass().hasInventoryStore(actor))
        {
            MWWorld::ContainerStore& store = actor.getClass().getContainerStore(actor);
            for (MWWorld::ContainerStoreIterator it = store.begin(); it != store.end(); ++it)
            {
                MWWorld::Ptr item = *it;
                const ConsumableAnimationSpec spec = resolveConsumable(actor, item);
                if (!spec.mGroup.empty() && animation->hasAnimation(spec.mGroup))
                    candidates.push_back(item);
            }
        }

        if (!candidates.empty())
        {
            const MWWorld::Ptr item
                = candidates[Misc::Rng::rollDice(static_cast<int>(candidates.size()))];
            ConsumableAnimationSpec spec = resolveConsumable(actor, item);
            if (!startConsumingAnimation(actor, std::move(spec), true))
                return false;
            queueNpcAnimPlay(actor, mwmp::encodeAmbientConsumableAnimation(item.getCellRef().getRefId()));
            return true;
        }

        // NPCs with no suitable food/drink still get a rare tavern-like generic
        // drink using the bundled goblet. Keeping this fallback uncommon prevents
        // every guard and shopkeeper from constantly materialising a cup.
        if (Misc::Rng::rollDice(100) >= 30 || !animation->hasAnimation("drinkbone"))
            return false;

        ConsumableAnimationSpec genericDrink;
        genericDrink.mGroup = "drinkbone";
        genericDrink.mPropModel = "meshes\\consan\\o_goblet_juicr.nif";
        genericDrink.mFallbackDuration = 2.6f;
        genericDrink.mPropRemoveTime = 2.2f;
        genericDrink.mShatterProp = false;
        if (!startConsumingAnimation(actor, std::move(genericDrink), true))
            return false;
        queueNpcAnimPlay(actor, mwmp::encodeAmbientConsumableAnimation(sAmbientGenericDrinkToken));
        return true;
    }

    bool playAmbientConsumableAnimation(const MWWorld::Ptr& actor, const std::string& refId)
    {
        if (actor.isEmpty() || refId.empty() || isConsumingAnimationActive(actor))
            return false;

        if (refId == sAmbientGenericDrinkToken)
        {
            ConsumableAnimationSpec genericDrink;
            genericDrink.mGroup = "drinkbone";
            genericDrink.mPropModel = "meshes\\consan\\o_goblet_juicr.nif";
            genericDrink.mFallbackDuration = 2.6f;
            genericDrink.mPropRemoveTime = 2.2f;
            genericDrink.mShatterProp = false;
            return startConsumingAnimation(actor, std::move(genericDrink), true);
        }

        MWWorld::Ptr item;
        if (actor.getClass().hasInventoryStore(actor))
        {
            MWWorld::ContainerStore& store = actor.getClass().getContainerStore(actor);
            const std::string wanted = lowerCase(refId);
            for (MWWorld::ContainerStoreIterator it = store.begin(); it != store.end(); ++it)
            {
                if (lowerCase(it->getCellRef().getRefId()) == wanted)
                {
                    item = *it;
                    break;
                }
            }
        }

        if (item.isEmpty())
            return false;

        ConsumableAnimationSpec spec = resolveConsumable(actor, item);
        if (spec.mGroup.empty())
            return false;
        return startConsumingAnimation(actor, std::move(spec), true);
    }

    bool isConsumingAnimationActive(const MWWorld::Ptr& ptr)
    {
        const int id = actorId(ptr);
        return id >= 0 && sConsumingAnimations.find(id) != sConsumingAnimations.end();
    }

    void updateConsumingAnimations(float dt)
    {
        for (auto it = sConsumingAnimations.begin(); it != sConsumingAnimations.end(); )
        {
            ConsumingAnimationState& state = it->second;
            if (state.mActor.isEmpty() || !state.mActor.isInCell())
            {
                it = sConsumingAnimations.erase(it);
                continue;
            }

            MWRender::Animation* animation
                = MWBase::Environment::get().getWorld()->getAnimation(state.mActor);
            if (!animation)
            {
                it = sConsumingAnimations.erase(it);
                continue;
            }

            state.mElapsed += std::max(0.f, dt);
            const MWMechanics::CreatureStats& stats
                = state.mActor.getClass().getCreatureStats(state.mActor);
            const bool dead = stats.isDead();
            const MWMechanics::Movement& movement
                = state.mActor.getClass().getMovementSettings(state.mActor);
            const bool ambientInterrupted = state.mAmbient
                && (stats.getKnockedDown() || stats.getAiSequence().isInCombat()
                    || stats.getDrawState() != MWMechanics::DrawState_Nothing
                    || std::abs(movement.mPosition[0]) > .05f
                    || std::abs(movement.mPosition[1]) > .05f
                    || std::abs(movement.mPosition[2]) > .05f
                    || MWBase::Environment::get().getWorld()->isSwimming(state.mActor));

            if (!dead && !ambientInterrupted && !state.mPropRemoved && !state.mPropModel.empty()
                && !isPropAttached(state.mPropHolder))
            {
                state.mPropHolder = attachHandProp(
                    animation, state.mPropModel, state.mPropBone);
            }

            if (!state.mPropRemoved && state.mPropRemoveTime >= 0.f
                && state.mElapsed >= state.mPropRemoveTime)
                removeConsumableProp(state, animation);

            if (!state.mSoundAtEnd && !state.mSoundPlayed && state.mSoundTime >= 0.f
                && state.mElapsed >= state.mSoundTime && !dead && !ambientInterrupted)
                playConsumableSound(state);

            const bool finished = dead || ambientInterrupted || state.mElapsed >= state.mDuration
                || !animation->isPlaying(state.mGroup);
            if (!finished)
            {
                ++it;
                continue;
            }

            finishConsumingAnimation(state, animation, !dead && !ambientInterrupted);
            it = sConsumingAnimations.erase(it);
        }
    }

    std::string getDynamicMovementAnimation(const MWWorld::Ptr& ptr,
        const std::string& baseGroup)
    {
        if (ptr.isEmpty() || !ptr.getClass().isNpc())
            return std::string();

        MWBase::World* world = MWBase::Environment::get().getWorld();
        const bool localPlayer = ptr == world->getPlayerPtr();

        // Dynamic Animations supplies separate lower-body locomotion sources
        // for the first-person player armature.
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
        if (localPlayer)
        {
            if (baseGroup == "walkforward" || baseGroup == "runforward")
            {
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
