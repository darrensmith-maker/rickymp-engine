#ifndef OPENMW_MECHANICS_OBSTACLE_H
#define OPENMW_MECHANICS_OBSTACLE_H

#include <osg/Vec3f>

namespace MWWorld
{
    class Ptr;
}

namespace MWMechanics
{
    struct Movement;

    /// tests actor's proximity to a closed door by default
    bool proximityToDoor(const MWWorld::Ptr& actor, float minDist);

    /// Returns door pointer within range. No guarantee is given as to which one
    /** \return Pointer to the door, or empty pointer if none exists **/
    const MWWorld::Ptr getNearbyDoor(const MWWorld::Ptr& actor, float minDist);

    class ObstacleCheck
    {
        public:
            ObstacleCheck();

            // Clear the timers and set the state machine to default
            void clear();

            bool isEvading() const;

            // Updates internal state, call each frame for moving actor
            void update(const MWWorld::Ptr& actor, const osg::Vec3f& destination, float duration);

            // Stop translational movement while the actor turns away from the obstacle.
            void takeEvasiveAction(MWMechanics::Movement& actorMovement) const;

            float getEvasionAngle() const;
            bool consumePathRebuildRequest();

        private:
            osg::Vec3f mPrev;

            enum class WalkState
            {
                Initial,
                Norm,
                CheckStuck,
                TurnAway,
                BackOff
            };
            WalkState mWalkState;

            float mStateDuration;
            float mEvasionAngle;
            float mInitialDistance = 0;
            bool mPathRebuildPending;

            void chooseEvasionAngle(const MWWorld::Ptr& actor);
    };
}

#endif
