#ifndef OPENMW_MWPHYSICS_RAYCASTING_H
#define OPENMW_MWPHYSICS_RAYCASTING_H

#include <osg/Quat>
#include <osg/Vec3f>

#include "../mwworld/ptr.hpp"

#include "collisiontype.hpp"

namespace MWPhysics
{
    struct RayCastingResult
    {
        bool mHit = false;
        float mHitFraction = 1.f;
        osg::Vec3f mHitPos;
        osg::Vec3f mHitNormal;
        MWWorld::Ptr mHitObject;
    };

    class RayCastingInterface
    {
        public:
            /// Get distance from \a point to the collision shape of \a target. Uses a raycast to find where the
            /// target vector hits the collision shape and then calculates distance from the intersection point.
            /// This can be used to find out how much nearer we need to move to the target for a "getHitContact" to be successful.
            /// \note Only Actor targets are supported at the moment.
            virtual float getHitDistance(const osg::Vec3f& point, const MWWorld::ConstPtr& target) const = 0;

            /// @param me Optional, a Ptr to ignore in the list of results. targets are actors to filter for, ignoring all other actors.
            virtual RayCastingResult castRay(const osg::Vec3f &from, const osg::Vec3f &to, const MWWorld::ConstPtr& ignore = MWWorld::ConstPtr(),
                    std::vector<MWWorld::Ptr> targets = std::vector<MWWorld::Ptr>(),
                    int mask = CollisionType_World|CollisionType_HeightMap|CollisionType_Actor|CollisionType_Door, int group=0xff) const = 0;

            virtual RayCastingResult castSphere(const osg::Vec3f& from, const osg::Vec3f& to, float radius,
                    const MWWorld::ConstPtr& ignore = MWWorld::ConstPtr()) const = 0;

            /// Continuous oriented-box sweep used by the native movable-prop physics.
            /// Unlike castSphere this follows both translation and rotation, so long or
            /// flat objects cannot rotate an end through a wall while their centre stays clear.
            virtual RayCastingResult castBox(const osg::Vec3f& from, const osg::Quat& fromRotation,
                    const osg::Vec3f& to, const osg::Quat& toRotation, const osg::Vec3f& halfExtents,
                    const MWWorld::ConstPtr& ignore = MWWorld::ConstPtr()) const = 0;

            /// Resolve a box which is already overlapping static collision geometry.
            /// This is intentionally separate from sweep tests: joint projection can move
            /// a ragdoll limb into a wall even when its integrated trajectory was clear.
            virtual osg::Vec3f getBoxPenetrationCorrection(const osg::Vec3f& position, const osg::Quat& rotation,
                    const osg::Vec3f& halfExtents, const MWWorld::ConstPtr& ignore, float maxCorrection) const = 0;

            /// Return true if actor1 can see actor2.
            virtual bool getLineOfSight(const MWWorld::ConstPtr& actor1, const MWWorld::ConstPtr& actor2) const = 0;
    };
}

#endif
