#include "actionopen.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/windowmanager.hpp"

#include "../mwmechanics/disease.hpp"

#include "interactionanimation.hpp"

namespace MWWorld
{
    ActionOpen::ActionOpen (const MWWorld::Ptr& container)
        : Action (false, container)
    {
    }

    void ActionOpen::executeImp (const MWWorld::Ptr& actor)
    {
        if (!InteractionAnimation::queueContainer(getTarget(), actor))
            InteractionAnimation::openContainerImmediately(getTarget(), actor);
    }
}
