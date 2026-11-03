#ifndef OPENMW_MWWORLD_INTERACTIONANIMATION_H
#define OPENMW_MWWORLD_INTERACTIONANIMATION_H

#include <string>

#include "ptr.hpp"

namespace MWWorld
{
    namespace InteractionAnimation
    {
        enum Prop
        {
            Prop_None = 0,
            Prop_Gold = 1,
            Prop_Parchment = 2,
            Prop_Fireball = 3
        };

        bool queueTake(const Ptr& object, const Ptr& actor);
        bool queueDoor(const Ptr& object, const Ptr& actor);
        bool queueContainer(const Ptr& object, const Ptr& actor);

        void takeImmediately(const Ptr& object, const Ptr& actor);
        void openContainerImmediately(const Ptr& object, const Ptr& actor);

        bool playOneShot(const std::string& group, int blendMask, float speed,
            float duration, int loops = 1, int prop = Prop_None,
            const std::string& propModel = std::string());
        bool playQuickLoot(const Ptr& container, bool takeAll);
        bool playReading(const Ptr& item);
        void stopReading();
        bool isActive();

        void update(float dt);
        void cancel();
    }
}

#endif
