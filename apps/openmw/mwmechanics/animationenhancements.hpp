#ifndef ARENAMW_ANIMATIONENHANCEMENTS_H
#define ARENAMW_ANIMATIONENHANCEMENTS_H

#include <string>

#include "../mwworld/ptr.hpp"

namespace ArenaMW
{
    struct InteractionAnimationData
    {
        std::string group;
        int blendMask = 0;
        float speed = 1.f;
        int loops = 1;
        float duration = 0.f;
        int prop = 0;
        std::string propModel;
    };

    bool playInteractionAnimation(const MWWorld::Ptr& ptr, const InteractionAnimationData& data);
    bool ensureInteractionAnimationProp(const MWWorld::Ptr& ptr, const InteractionAnimationData& data);
    void stopInteractionAnimation(const MWWorld::Ptr& ptr, const InteractionAnimationData& data);

    /// Dynamic Animations 1.14-compatible walking styles are stored locally.
    bool isValidWalkAnimationStyle(const std::string& group);
    void setWalkAnimationStyle(const MWWorld::Ptr& ptr, const std::string& group);
    void clearWalkAnimationStyle(const MWWorld::Ptr& ptr);
    std::string getWalkAnimationStyle(const MWWorld::Ptr& ptr);

    /// Player-selected full-body poses yield to locomotion, jumping, combat,
    /// swimming and scripted animations, then resume when the actor is idle.
    void setPersistentAnimation(const MWWorld::Ptr& ptr, const std::string& group,
        int blendMask, float speed);
    void clearPersistentAnimation(const MWWorld::Ptr& ptr);
    void updatePersistentAnimation(const MWWorld::Ptr& ptr, float dt);

    /// Native port of the user-supplied Consuming Animated package for the
    /// OpenMW 0.47-based ArenaMW branch. Call immediately before the item is
    /// actually consumed so its record/model are still valid.
    void notifyConsumableUsed(const MWWorld::Ptr& actor, const MWWorld::Ptr& item);
    /// Start a cosmetic, inventory-aware eating/drinking habit for an idle NPC.
    /// The selected item is never consumed or removed from the NPC inventory.
    bool tryStartAmbientNpcHabit(const MWWorld::Ptr& actor);
    /// Replay a cell-authority-selected cosmetic habit on a remote MP client.
    bool playAmbientConsumableAnimation(const MWWorld::Ptr& actor, const std::string& refId);
    void updateConsumingAnimations(float dt);
    bool isConsumingAnimationActive(const MWWorld::Ptr& ptr);

    /// Return a native 0.47 animation override for the current movement group.
    /// An empty result means that the stock CharacterController animation
    /// should be used.
    std::string getDynamicMovementAnimation(const MWWorld::Ptr& ptr,
        const std::string& baseGroup);
}

#endif
