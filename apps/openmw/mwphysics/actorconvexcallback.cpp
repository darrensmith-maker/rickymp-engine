#include "actorconvexcallback.hpp"
#include "collisiontype.hpp"
#include "contacttestwrapper.h"

#include <BulletCollision/CollisionDispatch/btCollisionObject.h>

#include <components/misc/convert.hpp>
#include <components/settings/settings.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwworld/class.hpp"

#include "actor.hpp"
#include "projectile.hpp"

#include <algorithm>
#include <cmath>

namespace MWPhysics
{
    namespace
    {
        float clampPassScale(float value)
        {
            // Values below this start to feel like actors can walk through each other,
            // while values above 1 would make the passage problem worse.
            return std::clamp(value, 0.35f, 1.0f);
        }

        float getActorPassScale(const Actor* actorA, const Actor* actorB)
        {
            if (actorA == nullptr || actorB == nullptr)
                return 1.f;

            const MWWorld::ConstPtr ptrA = actorA->getPtr();
            const MWWorld::ConstPtr ptrB = actorB->getPtr();

            // Keep creatures and non-NPC actors on the original collision behaviour.
            // The compact passage feature is intended specifically for humanoid NPCs.
            if (!ptrA.getClass().isNpc() || !ptrB.getClass().isNpc())
                return 1.f;

            const MWWorld::ConstPtr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
            const bool involvesPlayer = ptrA == player || ptrB == player;
            static const float playerNpcScale
                = clampPassScale(Settings::Manager::getFloat("npc pass collision scale", "Game"));
            static const float npcNpcScale
                = clampPassScale(Settings::Manager::getFloat("npc to npc collision scale", "Game"));
            return involvesPlayer ? playerNpcScale : npcNpcScale;
        }

        bool compactActorSweep(const Actor* actorA, const Actor* actorB, const btCollisionObject* objectA,
            const btCollisionObject* objectB, const btVector3& invertedMotion, float scale,
            btScalar& hitFraction, btVector3& hitNormal)
        {
            if (actorA == nullptr || actorB == nullptr || scale >= 0.999f)
                return false;

            // Actor-vs-actor passage uses a cylinder-like horizontal footprint while
            // keeping the original collision boxes registered in Bullet. As a result,
            // melee/projectile/raycast hit detection and world collision remain unchanged.
            const osg::Vec3f halfA = actorA->getHalfExtents();
            const osg::Vec3f halfB = actorB->getHalfExtents();
            const btScalar radiusA = std::max(halfA.x(), halfA.y()) * scale;
            const btScalar radiusB = std::max(halfB.x(), halfB.y()) * scale;
            const btScalar combinedRadius = radiusA + radiusB;

            const btVector3 start = objectA->getWorldTransform().getOrigin();
            const btVector3 end = start - invertedMotion;
            const btVector3 other = objectB->getWorldTransform().getOrigin();

            btVector3 relative = start - other;
            btVector3 movement = end - start;
            relative.setZ(0);
            movement.setZ(0);

            const btScalar a = movement.length2();
            const btScalar c = relative.length2() - combinedRadius * combinedRadius;
            btScalar fraction = 0;

            if (c > 0)
            {
                if (a <= SIMD_EPSILON)
                    return false;

                const btScalar b = 2 * relative.dot(movement);
                const btScalar discriminant = b * b - 4 * a * c;
                if (discriminant < 0)
                    return false;

                fraction = (-b - btSqrt(discriminant)) / (2 * a);
                if (fraction < 0 || fraction > 1)
                    return false;
            }

            // Do not make actors on different vertical levels block each other.
            const btScalar zAtHit = start.z() + (end.z() - start.z()) * fraction;
            const btScalar verticalReach = halfA.z() + halfB.z();
            if (btFabs(zAtHit - other.z()) > verticalReach)
                return false;

            btVector3 normal = relative + movement * fraction;
            normal.setZ(0);
            if (normal.length2() <= SIMD_EPSILON)
            {
                normal = -movement;
                normal.setZ(0);
            }
            if (normal.length2() <= SIMD_EPSILON)
                return false;

            normal.normalize();
            hitFraction = fraction;
            hitNormal = normal;
            return true;
        }
    }

    class ActorOverlapTester : public btCollisionWorld::ContactResultCallback
    {
    public:
        bool overlapping = false;

        btScalar addSingleResult(btManifoldPoint& cp,
            const btCollisionObjectWrapper* colObj0Wrap,
            int partId0,
            int index0,
            const btCollisionObjectWrapper* colObj1Wrap,
            int partId1,
            int index1) override
        {
            if(cp.getDistance() <= 0.0f)
                overlapping = true;
            return btScalar(1);
        }
    };

    ActorConvexCallback::ActorConvexCallback(const btCollisionObject *me, const btVector3 &motion, btScalar minCollisionDot, const btCollisionWorld * world)
    : btCollisionWorld::ClosestConvexResultCallback(btVector3(0.0, 0.0, 0.0), btVector3(0.0, 0.0, 0.0)),
      mMe(me), mMotion(motion), mMinCollisionDot(minCollisionDot), mWorld(world)
    {
    }

    btScalar ActorConvexCallback::addSingleResult(btCollisionWorld::LocalConvexResult& convexResult, bool normalInWorldSpace)
    {
        if (convexResult.m_hitCollisionObject == mMe)
            return btScalar(1);

        // override data for actor-actor collisions
        // vanilla Morrowind seems to make overlapping actors collide as though they are both cylinders with a diameter of the distance between them
        // For some reason this doesn't work as well as it should when using capsules, but it still helps a lot.
        if(convexResult.m_hitCollisionObject->getBroadphaseHandle()->m_collisionFilterGroup == CollisionType_Actor)
        {
            const auto* meActor = static_cast<const Actor*>(mMe->getUserPointer());
            const auto* hitActor = static_cast<const Actor*>(convexResult.m_hitCollisionObject->getUserPointer());
            const float passScale = getActorPassScale(meActor, hitActor);

            if (passScale < 0.999f)
            {
                btScalar compactFraction = 1;
                btVector3 compactNormal(0, 0, 0);
                if (!compactActorSweep(meActor, hitActor, mMe, convexResult.m_hitCollisionObject, mMotion,
                        passScale, compactFraction, compactNormal))
                    return btScalar(1);

                // If another obstacle is already closer, this actor must not replace it.
                if (compactFraction >= m_closestHitFraction)
                    return btScalar(1);

                // The motion vector is intentionally inverted in ActorTracer.
                if (compactNormal.dot(mMotion) > 0.0f)
                {
                    convexResult.m_hitFraction = compactFraction;
                    convexResult.m_hitNormalLocal = compactNormal;
                    return ClosestConvexResultCallback::addSingleResult(convexResult, true);
                }
                return btScalar(1);
            }

            // Original OpenMW/vanilla-compatible actor collision path.
            ActorOverlapTester isOverlapping;
            // FIXME: This is absolutely terrible and bullet should feel terrible for not making contactPairTest const-correct.
            ContactTestWrapper::contactPairTest(const_cast<btCollisionWorld*>(mWorld), const_cast<btCollisionObject*>(mMe), const_cast<btCollisionObject*>(convexResult.m_hitCollisionObject), isOverlapping);

            if(isOverlapping.overlapping)
            {
                auto originA = Misc::Convert::toOsg(mMe->getWorldTransform().getOrigin());
                auto originB = Misc::Convert::toOsg(convexResult.m_hitCollisionObject->getWorldTransform().getOrigin());
                osg::Vec3f motion = Misc::Convert::toOsg(mMotion);
                osg::Vec3f normal = (originA-originB);
                normal.z() = 0;
                normal.normalize();
                // only collide if horizontally moving towards the hit actor (note: the motion vector appears to be inverted)
                // FIXME: This kinda screws with standing on actors that walk up slopes for some reason. Makes you fall through them.
                // It happens in vanilla Morrowind too, but much less often.
                // I tried hunting down why but couldn't figure it out. Possibly a stair stepping or ground ejection bug.
                if(normal * motion > 0.0f)
                {
                    convexResult.m_hitFraction = 0.0f;
                    convexResult.m_hitNormalLocal = Misc::Convert::toBullet(normal);
                    return ClosestConvexResultCallback::addSingleResult(convexResult, true);
                }
                else
                {
                    return btScalar(1);
                }
            }
        }
        if (convexResult.m_hitCollisionObject->getBroadphaseHandle()->m_collisionFilterGroup == CollisionType_Projectile)
        {
            auto* projectileHolder = static_cast<Projectile*>(convexResult.m_hitCollisionObject->getUserPointer());
            if (!projectileHolder->isActive())
                return btScalar(1);
            auto* targetHolder = static_cast<PtrHolder*>(mMe->getUserPointer());
            const MWWorld::Ptr target = targetHolder->getPtr();
            if (projectileHolder->isValidTarget(target))
                projectileHolder->hit(target, convexResult.m_hitPointLocal, convexResult.m_hitNormalLocal);
            return btScalar(1);
        }

        btVector3 hitNormalWorld;
        if (normalInWorldSpace)
            hitNormalWorld = convexResult.m_hitNormalLocal;
        else
        {
            ///need to transform normal into worldspace
            hitNormalWorld = convexResult.m_hitCollisionObject->getWorldTransform().getBasis()*convexResult.m_hitNormalLocal;
        }

        // dot product of the motion vector against the collision contact normal
        btScalar dotCollision = mMotion.dot(hitNormalWorld);
        if (dotCollision <= mMinCollisionDot)
            return btScalar(1);

        return ClosestConvexResultCallback::addSingleResult(convexResult, normalInWorldSpace);
    }
}
