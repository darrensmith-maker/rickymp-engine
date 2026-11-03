#include "interactionanimation.hpp"

#include <algorithm>
#include <cmath>
#include <deque>

#include <osg/Vec3f>

#include <components/settings/settings.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/disease.hpp"
#include "../mwmechanics/movement.hpp"

#include "../mwmp/LocalPlayer.hpp"
#include "../mwmp/Main.hpp"
#include "../mwmp/Networking.hpp"
#include "../mwmp/ObjectList.hpp"

#include "../mwrender/animation.hpp"

#include "class.hpp"
#include "containerstore.hpp"
#include "doorstate.hpp"

namespace
{
    enum class PendingType
    {
        None,
        Take,
        Door,
        Container
    };

    struct InteractionRequest
    {
        PendingType mType = PendingType::None;
        MWWorld::Ptr mObject;
        MWWorld::Ptr mActor;
        std::string mGroup;
        int mBlendMask = 0;
        float mSpeed = 1.f;
        float mDuration = 0.f;
        float mTriggerTime = 0.f;
        bool mMoveObject = false;
    };

    struct PendingInteraction : InteractionRequest
    {
        osg::Vec3f mStartPosition;
        float mElapsed = 0.f;
        bool mTriggered = false;
    };

    PendingInteraction sPending;
    std::deque<InteractionRequest> sQueue;
    float sItemCooldown = 0.f;

    bool enabled(const char* setting)
    {
        return Settings::Manager::getBool("animated interactions", "GUI")
            && Settings::Manager::getBool(setting, "GUI");
    }

    bool isPlayer(const MWWorld::Ptr& actor)
    {
        return !actor.isEmpty()
            && actor == MWBase::Environment::get().getWorld()->getPlayerPtr();
    }

    bool actorBlocksInteraction(const MWWorld::Ptr& actor)
    {
        if (actor.isEmpty() || !actor.isInCell())
            return true;

        const MWMechanics::CreatureStats& stats = actor.getClass().getCreatureStats(actor);
        const MWMechanics::Movement& movement = actor.getClass().getMovementSettings(actor);
        const bool moving = std::abs(movement.mPosition[0]) > 0.05f
            || std::abs(movement.mPosition[1]) > 0.05f
            || std::abs(movement.mPosition[2]) > 0.05f;

        return moving || stats.isDead() || stats.getKnockedDown();
    }

    osg::Vec3f getHandDestination(const MWWorld::Ptr& actor)
    {
        MWBase::World* world = MWBase::Environment::get().getWorld();
        osg::Vec3f destination = world->getActorHeadTransform(actor).getTrans();
        const ESM::Position& actorPosition = actor.getRefData().getPosition();
        const float yaw = actorPosition.rot[2];

        // An approximate right-hand point. The animated hand converges on this
        // point closely in both first- and third-person skeletons.
        destination.x() += std::cos(yaw) * 14.f + std::sin(yaw) * 10.f;
        destination.y() += std::sin(yaw) * 14.f - std::cos(yaw) * 10.f;
        destination.z() -= 22.f;
        return destination;
    }

    bool beginPending(const InteractionRequest& request)
    {
        mwmp::LocalPlayer* localPlayer = mwmp::Main::get().getLocalPlayer();
        if (!localPlayer || localPlayer->isInteractionAnimationPlaying()
            || !localPlayer->playInteractionAnimation(request.mGroup, request.mBlendMask,
                request.mSpeed, 1, request.mDuration,
                MWWorld::InteractionAnimation::Prop_None))
            return false;

        sPending = PendingInteraction();
        static_cast<InteractionRequest&>(sPending) = request;
        sPending.mElapsed = 0.f;
        sPending.mTriggerTime = std::max(0.01f, request.mTriggerTime);
        sPending.mDuration = std::max(sPending.mTriggerTime, request.mDuration);
        sPending.mMoveObject = request.mMoveObject && request.mObject.isInCell();
        if (sPending.mMoveObject)
            sPending.mStartPosition = request.mObject.getRefData().getPosition().asVec3();
        return true;
    }

    bool requestAlreadyQueued(PendingType type, const MWWorld::Ptr& object)
    {
        for (const InteractionRequest& request : sQueue)
        {
            if (request.mType == type && request.mObject == object)
                return true;
        }
        return false;
    }

    bool startPending(PendingType type, const MWWorld::Ptr& object, const MWWorld::Ptr& actor,
        const std::string& group, int blendMask, float speed, float duration,
        float triggerTime, bool moveObject)
    {
        if (!isPlayer(actor) || object.isEmpty() || actorBlocksInteraction(actor))
            return false;

        InteractionRequest request;
        request.mType = type;
        request.mObject = object;
        request.mActor = actor;
        request.mGroup = group;
        request.mBlendMask = blendMask;
        request.mSpeed = speed;
        request.mDuration = duration;
        request.mTriggerTime = triggerTime;
        request.mMoveObject = moveObject;

        mwmp::LocalPlayer* localPlayer = mwmp::Main::get().getLocalPlayer();
        if (sPending.mType != PendingType::None
            || (localPlayer && localPlayer->isInteractionAnimationPlaying()))
        {
            if (sPending.mType == type && sPending.mObject == object)
                return true;
            if (!requestAlreadyQueued(type, object) && sQueue.size() < 16)
                sQueue.push_back(request);
            return true;
        }

        return beginPending(request);
    }

    void restoreMovedObject()
    {
        if (!sPending.mMoveObject || sPending.mTriggered || sPending.mObject.isEmpty()
            || !sPending.mObject.isInCell() || sPending.mActor.isEmpty()
            || !sPending.mActor.isInCell() || sPending.mObject.getCell() != sPending.mActor.getCell())
            return;

        MWBase::Environment::get().getWorld()->moveObject(
            sPending.mObject, sPending.mObject.getCell(),
            sPending.mStartPosition.x(), sPending.mStartPosition.y(),
            sPending.mStartPosition.z(), false);
    }

    bool pendingWasInterrupted()
    {
        if (sPending.mType == PendingType::None || sPending.mTriggered)
            return false;
        if (sPending.mActor.isEmpty() || !sPending.mActor.isInCell())
            return true;
        if (sPending.mObject.isEmpty() || !sPending.mObject.isInCell()
            || sPending.mObject.getRefData().getCount() <= 0
            || sPending.mObject.getCell() != sPending.mActor.getCell())
            return true;

        return actorBlocksInteraction(sPending.mActor)
            || MWBase::Environment::get().getWindowManager()->isGuiMode();
    }

    void executeRequestImmediately(const InteractionRequest& request)
    {
        switch (request.mType)
        {
            case PendingType::Take:
                MWWorld::InteractionAnimation::takeImmediately(request.mObject, request.mActor);
                break;
            case PendingType::Door:
                if (!request.mObject.isEmpty() && request.mObject.isInCell())
                    MWBase::Environment::get().getWorld()->activateDoor(request.mObject);
                break;
            case PendingType::Container:
                MWWorld::InteractionAnimation::openContainerImmediately(
                    request.mObject, request.mActor);
                break;
            case PendingType::None:
                break;
        }
    }

    void finishPendingAction()
    {
        if (sPending.mTriggered)
            return;
        sPending.mTriggered = true;
        executeRequestImmediately(sPending);
    }

    void startNextQueuedInteraction()
    {
        if (sPending.mType != PendingType::None)
            return;

        if (MWBase::Environment::get().getWindowManager()->isGuiMode())
        {
            sQueue.clear();
            return;
        }

        mwmp::LocalPlayer* localPlayer = mwmp::Main::get().getLocalPlayer();
        if (localPlayer && localPlayer->isInteractionAnimationPlaying())
            return;

        while (!sQueue.empty())
        {
            const InteractionRequest request = sQueue.front();
            sQueue.pop_front();

            if (!isPlayer(request.mActor) || request.mObject.isEmpty()
                || !request.mObject.isInCell()
                || request.mObject.getRefData().getCount() <= 0)
                continue;

            if (actorBlocksInteraction(request.mActor))
            {
                // Movement, jumping or incapacitation invalidates the rest of
                // the activation chain. Never convert queued animated actions
                // into surprise instant activations.
                sQueue.clear();
                return;
            }

            if (beginPending(request))
                return;

            // Missing animation resources must never block the underlying action.
            executeRequestImmediately(request);
        }
    }
}

namespace MWWorld
{
    namespace InteractionAnimation
    {
        bool playOneShot(const std::string& group, int blendMask, float speed,
            float duration, int loops, int prop, const std::string& propModel)
        {
            mwmp::LocalPlayer* localPlayer = mwmp::Main::get().getLocalPlayer();
            return localPlayer && localPlayer->playInteractionAnimation(
                group, blendMask, speed, loops, duration, prop, propModel);
        }

        bool queueTake(const Ptr& object, const Ptr& actor)
        {
            if (!enabled("animated item pickup")
                || MWBase::Environment::get().getWindowManager()->isGuiMode()
                || sItemCooldown > 0.f)
                return false;

            const float speed = std::clamp(
                Settings::Manager::getFloat("interaction item speed", "GUI"), 0.5f, 3.f);
            const float duration = 0.60f / speed;
            const bool started = startPending(PendingType::Take, object, actor, "loot1",
                MWRender::Animation::BlendMask_Torso | MWRender::Animation::BlendMask_RightArm,
                speed, duration, duration * 0.72f,
                Settings::Manager::getBool("animated pickup movement", "GUI"));
            if (started)
                sItemCooldown = std::max(0.f,
                    Settings::Manager::getFloat("interaction item cooldown", "GUI"));
            return started;
        }

        bool queueDoor(const Ptr& object, const Ptr& actor)
        {
            if (object.isEmpty() || !enabled("animated doors")
                || object.getCellRef().getTeleport())
                return false;

            const DoorState state = object.getClass().getDoorState(object);
            const float currentRotation = object.getRefData().getPosition().rot[2]
                - object.getCellRef().getPosition().rot[2];
            const bool closing = state == DoorState::Opening
                || (state == DoorState::Idle && std::abs(currentRotation) > 0.001f);

            return startPending(PendingType::Door, object, actor,
                closing ? "loot4" : "loot2",
                MWRender::Animation::BlendMask_Torso | MWRender::Animation::BlendMask_RightArm,
                2.f, 0.33f, 0.22f, false);
        }

        bool queueContainer(const Ptr& object, const Ptr& actor)
        {
            if (object.isEmpty() || !enabled("animated containers"))
                return false;
            if (object.getClass().isActor()
                && !object.getClass().getCreatureStats(object).isDead())
                return false;

            return startPending(PendingType::Container, object, actor, "loot3",
                MWRender::Animation::BlendMask_Torso | MWRender::Animation::BlendMask_RightArm,
                2.f, 0.36f, 0.24f, false);
        }

        void takeImmediately(const Ptr& object, const Ptr& actor)
        {
            if (object.isEmpty() || actor.isEmpty() || !object.isInCell()
                || object.getRefData().getCount() <= 0)
                return;

            MWBase::Environment::get().getMechanicsManager()->itemTaken(
                actor, object, MWWorld::Ptr(), object.getRefData().getCount());
            MWWorld::Ptr newItem = *actor.getClass().getContainerStore(actor).add(
                object, object.getRefData().getCount(), actor);

            mwmp::ObjectList* objectList = mwmp::Main::get().getNetworking()->getObjectList();
            objectList->reset();
            objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
            objectList->addObjectGeneric(object);
            objectList->sendObjectDelete();

            MWBase::Environment::get().getWorld()->deleteObject(object);
            (void)newItem;
        }

        void openContainerImmediately(const Ptr& object, const Ptr& actor)
        {
            if (object.isEmpty() || actor != MWMechanics::getPlayer())
                return;
            if (!MWBase::Environment::get().getWindowManager()->isAllowed(MWGui::GW_Inventory))
                return;
            if (!MWBase::Environment::get().getMechanicsManager()->onOpen(object))
                return;

            MWMechanics::diseaseContact(actor, object);
            MWBase::Environment::get().getWindowManager()->pushGuiMode(MWGui::GM_Container, object);
        }

        bool playQuickLoot(const Ptr& container, bool takeAll)
        {
            if (!enabled("animated quickloot") || container.isEmpty())
                return false;

            if (takeAll)
            {
                return playOneShot("loot3",
                    MWRender::Animation::BlendMask_Torso | MWRender::Animation::BlendMask_RightArm,
                    2.f, 0.36f);
            }

            const float speed = std::clamp(
                Settings::Manager::getFloat("interaction item speed", "GUI"), 0.5f, 3.f);
            return playOneShot("loot1",
                MWRender::Animation::BlendMask_Torso | MWRender::Animation::BlendMask_RightArm,
                speed, 0.60f / speed);
        }

        bool playReading(const Ptr& item)
        {
            if (item.isEmpty())
                return false;

            mwmp::LocalPlayer* localPlayer = mwmp::Main::get().getLocalPlayer();
            if (!localPlayer)
                return false;
            if (localPlayer->isInteractionAnimationPlaying())
                localPlayer->cancelInteractionAnimation(true);

            const std::string model = item.getClass().getModel(item);
            const int fallbackProp = model.empty() ? Prop_Parchment : Prop_None;

            // Use the exact world model of the opened book or scroll. The generic
            // parchment prop is now only a fallback for records without a model.
            // The long duration is cancelled by Book/ScrollWindow::onClose and the
            // active state is periodically refreshed for players entering the AOI.
            return playOneShot("read-paper", MWRender::Animation::BlendMask_UpperBody,
                1.f, 600.f, 100, fallbackProp, model);
        }

        void stopReading()
        {
            if (mwmp::LocalPlayer* localPlayer = mwmp::Main::get().getLocalPlayer())
                localPlayer->cancelInteractionAnimation(true);
        }

        bool isActive()
        {
            mwmp::LocalPlayer* localPlayer = mwmp::Main::get().getLocalPlayer();
            return sPending.mType != PendingType::None
                || (localPlayer && localPlayer->isInteractionAnimationPlaying());
        }

        void update(float dt)
        {
            if (dt <= 0.f)
                return;

            sItemCooldown = std::max(0.f, sItemCooldown - dt);
            if (sPending.mType == PendingType::None)
            {
                startNextQueuedInteraction();
                return;
            }

            if (pendingWasInterrupted())
            {
                cancel();
                return;
            }

            sPending.mElapsed += dt;
            if (sPending.mMoveObject && !sPending.mTriggered
                && !sPending.mObject.isEmpty() && sPending.mObject.isInCell()
                && !sPending.mActor.isEmpty() && sPending.mActor.isInCell()
                && sPending.mObject.getCell() == sPending.mActor.getCell())
            {
                float t = std::clamp(sPending.mElapsed / sPending.mTriggerTime, 0.f, 1.f);
                t = t * t * (3.f - 2.f * t);
                const osg::Vec3f destination = getHandDestination(sPending.mActor);
                const osg::Vec3f position = sPending.mStartPosition
                    + (destination - sPending.mStartPosition) * t;
                MWBase::Environment::get().getWorld()->moveObject(
                    sPending.mObject, sPending.mObject.getCell(),
                    position.x(), position.y(), position.z(), false);
            }

            if (!sPending.mTriggered && sPending.mElapsed >= sPending.mTriggerTime)
                finishPendingAction();

            if (sPending.mElapsed >= sPending.mDuration)
            {
                sPending = PendingInteraction();
                startNextQueuedInteraction();
            }
        }

        void cancel()
        {
            restoreMovedObject();
            sPending = PendingInteraction();
            sQueue.clear();
            if (mwmp::LocalPlayer* localPlayer = mwmp::Main::get().getLocalPlayer())
                localPlayer->cancelInteractionAnimation(true);
        }
    }
}
