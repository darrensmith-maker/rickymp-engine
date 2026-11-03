#include "aiactivate.hpp"

#include <algorithm>
#include <cstddef>
#include <components/esm/aisequence.hpp>
#include <components/misc/rng.hpp>
#include <components/misc/stringops.hpp>
#include <components/settings/settings.hpp>

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include "../mwmp/Main.hpp"
#include "../mwmp/Networking.hpp"
#include "../mwmp/ObjectList.hpp"
#include "../mwmp/InteractionAnimationSync.hpp"
#include "../mwmp/LocalActor.hpp"
#include "../mwmp/CellController.hpp"
/*
    End of tes3mp addition
*/

#include "../mwbase/world.hpp"
#include "../mwbase/environment.hpp"

#include "../mwworld/class.hpp"

#include "../mwrender/animation.hpp"

#include "creaturestats.hpp"
#include "movement.hpp"
#include "steering.hpp"
#include "animationenhancements.hpp"

namespace
{
    bool isWorshipTarget(const MWWorld::Ptr& target)
    {
        if (target.isEmpty())
            return false;

        std::string signature = target.getCellRef().getRefId();
        signature += " ";
        signature += target.getClass().getName(target);
        signature += " ";
        signature += target.getClass().getModel(target);
        Misc::StringUtils::lowerCaseInPlace(signature);

        return signature.find("altar") != std::string::npos
            || signature.find("shrine") != std::string::npos
            || signature.find("prayer") != std::string::npos;
    }

    void queueWorshipAnimation(const MWWorld::Ptr& actor, const std::string& group, int blendMask,
        float speed, bool stop);

    bool startWorshipAnimation(const MWWorld::Ptr& actor, std::string& group, int& blendMask)
    {
        MWRender::Animation* animation = MWBase::Environment::get().getWorld()->getAnimation(actor);
        if (!animation)
            return false;

        struct Candidate
        {
            const char* mGroup;
            int mBlendMask;
            float mSpeed;
        };
        static const Candidate sCandidates[] = {
            { "prayer1", MWRender::Animation::BlendMask_All, 0.82f },
            { "prayer2", MWRender::Animation::BlendMask_All, 0.82f },
            { "armsAlmaPray", MWRender::Animation::BlendMask_UpperBody, 0.76f },
            { "PoseAlma3", MWRender::Animation::BlendMask_UpperBody, 0.82f },
        };

        const int first = Misc::Rng::rollDice(static_cast<int>(sizeof(sCandidates) / sizeof(sCandidates[0])));
        for (std::size_t offset = 0; offset < sizeof(sCandidates) / sizeof(sCandidates[0]); ++offset)
        {
            const Candidate& candidate
                = sCandidates[(static_cast<std::size_t>(first) + offset)
                    % (sizeof(sCandidates) / sizeof(sCandidates[0]))];
            if (!animation->hasAnimation(candidate.mGroup))
                continue;

            ArenaMW::InteractionAnimationData data;
            data.group = candidate.mGroup;
            data.blendMask = candidate.mBlendMask;
            data.speed = candidate.mSpeed;
            data.loops = 1;
            if (ArenaMW::playInteractionAnimation(actor, data))
            {
                group = candidate.mGroup;
                blendMask = candidate.mBlendMask;
                queueWorshipAnimation(actor, group, blendMask, candidate.mSpeed, false);
                return true;
            }
        }
        return false;
    }

    void stopWorshipAnimation(const MWWorld::Ptr& actor, const std::string& group, int blendMask)
    {
        if (group.empty() || blendMask == 0)
            return;
        ArenaMW::InteractionAnimationData data;
        data.group = group;
        data.blendMask = blendMask;
        ArenaMW::stopInteractionAnimation(actor, data);
        queueWorshipAnimation(actor, group, blendMask, 1.f, true);
    }

    void queueWorshipAnimation(const MWWorld::Ptr& actor, const std::string& group, int blendMask,
        float speed, bool stop)
    {
        if (actor.isEmpty() || group.empty() || blendMask == 0)
            return;
        mwmp::CellController* cellController = mwmp::Main::get().getCellController();
        if (!cellController || !cellController->isLocalActor(actor))
            return;
        mwmp::LocalActor* localActor = cellController->getLocalActor(actor);
        if (!localActor)
            return;
        mwmp::InteractionAnimationData data;
        data.group = group;
        data.blendMask = blendMask;
        data.speed = speed;
        data.loops = 1;
        data.duration = stop ? 0.1f : 3.f;
        data.stop = stop;
        const std::string encoded = mwmp::encodeInteractionAnimation(data);
        if (!encoded.empty())
        {
            localActor->animation.groupname = encoded;
            localActor->animation.mode = 0;
            localActor->animation.count = 1;
            localActor->animation.persist = false;
        }
    }
}


namespace MWMechanics
{
    AiActivate::AiActivate(const std::string &objectId)
        : mObjectId(objectId)
    {
    }

    /*
        Start of tes3mp addition

        Allow AiActivate to be initialized using a Ptr instead of a refId
    */
    AiActivate::AiActivate(MWWorld::Ptr object)
        : mObjectId("")
    {
        mObjectPtr = object;
    }
    /*
        End of tes3mp addition
    */

    bool AiActivate::execute(const MWWorld::Ptr& actor, CharacterController& characterController, AiState& state, float duration)
    {
        const MWWorld::Ptr target = mObjectId.empty() ? mObjectPtr
            : MWBase::Environment::get().getWorld()->searchPtr(mObjectId, false);

        actor.getClass().getCreatureStats(actor).setDrawState(DrawState_Nothing);

        if (target == MWWorld::Ptr() || !target.getRefData().getCount() || !target.getRefData().isEnabled())
        {
            if (mContextActionStarted)
                stopWorshipAnimation(actor, mContextAnimation, mContextBlendMask);
            mContextActionStarted = false;
            mContextActionCompleted = false;
            mContextAnimation.clear();
            mContextBlendMask = 0;
            return true;
        }

        const osg::Vec3f targetDir = target.getRefData().getPosition().asVec3() - actor.getRefData().getPosition().asVec3();
        zTurn(actor, std::atan2(targetDir.x(), targetDir.y()), 0.f);
        actor.getClass().getMovementSettings(actor).mPosition[1] = 1;
        actor.getClass().getMovementSettings(actor).mPosition[0] = 0;

        const float activationDistance = MWBase::Environment::get().getWorld()->getMaxActivationDistance();
        if (mContextActionStarted && targetDir.length() > activationDistance)
        {
            stopWorshipAnimation(actor, mContextAnimation, mContextBlendMask);
            mContextActionStarted = false;
            mContextActionTimer = 0.f;
            mContextAnimation.clear();
            mContextBlendMask = 0;
        }

        if (activationDistance >= targetDir.length())
        {
            actor.getClass().getMovementSettings(actor).mPosition[1] = 0;
            actor.getClass().getMovementSettings(actor).mPosition[0] = 0;

            if (!mContextActionCompleted && actor.getClass().isNpc()
                && Settings::Manager::getBool("contextual npc animations", "GUI")
                && isWorshipTarget(target))
            {
                if (!mContextActionStarted)
                {
                    mContextAnimation.clear();
                    mContextBlendMask = 0;
                    if (startWorshipAnimation(actor, mContextAnimation, mContextBlendMask))
                    {
                        mContextActionStarted = true;
                        mContextActionTimer = 2.4f;
                        return false;
                    }
                    mContextActionCompleted = true;
                }
                else
                {
                    mContextActionTimer -= std::max(0.f, duration);
                    if (mContextActionTimer > 0.f)
                        return false;
                    stopWorshipAnimation(actor, mContextAnimation, mContextBlendMask);
                    mContextAnimation.clear();
                    mContextBlendMask = 0;
                    mContextActionStarted = false;
                    mContextActionCompleted = true;
                }
            }

            mwmp::ObjectList *objectList = mwmp::Main::get().getNetworking()->getObjectList();
            objectList->reset();
            objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
            objectList->addObjectActivate(target, actor);
            objectList->sendObjectActivate();
            return true;
        }
        return false;
    }

    void AiActivate::writeState(ESM::AiSequence::AiSequence &sequence) const
    {
        std::unique_ptr<ESM::AiSequence::AiActivate> activate(new ESM::AiSequence::AiActivate());
        activate->mTargetId = mObjectId;

        ESM::AiSequence::AiPackageContainer package;
        package.mType = ESM::AiSequence::Ai_Activate;
        package.mPackage = activate.release();
        sequence.mPackages.push_back(package);
    }

    AiActivate::AiActivate(const ESM::AiSequence::AiActivate *activate)
        : mObjectId(activate->mTargetId)
    {
    }
}
