#include "actiondoor.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"

#include "interactionanimation.hpp"

namespace MWWorld
{
    ActionDoor::ActionDoor (const MWWorld::Ptr& object) : Action (false, object)
    {
    }

    void ActionDoor::executeImp (const MWWorld::Ptr& actor)
    {
        if (!InteractionAnimation::queueDoor(getTarget(), actor))
            MWBase::Environment::get().getWorld()->activateDoor(getTarget());
    }
}
