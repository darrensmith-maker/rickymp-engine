#ifndef OPENMW_MWPHYSICS_RAGDOLL_H
#define OPENMW_MWPHYSICS_RAGDOLL_H

#include <string>
#include <vector>

#include <osg/Matrixf>
#include <osg/Quat>
#include <osg/Vec3f>
#include <osg/observer_ptr>

#include "../mwworld/ptr.hpp"

namespace osg
{
    class MatrixTransform;
}

namespace MWRender
{
    class Animation;
}

namespace MWPhysics
{
    class RayCastingInterface;

    /// Stable game-oriented ragdoll for Morrowind biped skeletons.
    ///
    /// The renderer still uses the original skeleton and fixed bone lengths. The
    /// simulation works on joint particles with PBD-style constraints, joint
    /// projection, continuous world sweeps and a final penetration recovery pass.
    /// This gives the useful stability properties of modern rigid-body ragdolls
    /// without replacing OpenMW 0.47's collision-only Bullet world.
    class Ragdoll
    {
    public:
        Ragdoll(const MWWorld::Ptr& actor, MWRender::Animation* animation,
            const RayCastingInterface* rayCasting);
        ~Ragdoll();

        bool initialize();
        void update(float dt);
        void stop();

        bool isValid() const { return mValid; }
        bool isSleeping() const { return mSleeping; }
        const MWWorld::Ptr& getActor() const { return mActor; }

    private:
        struct Bone
        {
            std::string mName;
            std::string mParentName;
            osg::observer_ptr<osg::MatrixTransform> mNode;
            int mParent = -1;
            int mDirectionChild = -1;
            osg::Vec3f mPosition;
            osg::Vec3f mPreviousPosition;
            osg::Vec3f mInitialPosition;
            osg::Quat mInitialWorldRotation;
            osg::Matrixf mInitialLocalMatrix;
            osg::Vec3f mInitialLocalTranslation;
            osg::Vec3f mContactNormal = osg::Vec3f(0.f, 0.f, 1.f);
            float mRestLength = 0.f;
            float mRadius = 4.f;
            float mMass = 1.f;
            bool mContacted = false;
            bool mSupported = false;
        };

        bool addBone(const char* name, const char* parent);
        void resolveHierarchy();
        void seedVelocity();
        void simulateStep(float dt);
        void solveConstraints(int iterations, bool shockPropagation);
        void solveJointLimits();
        void solveSelfCollisions();
        void resetContacts();
        void projectWorldContacts(bool finalPass);
        void collideBones(bool finalPass);
        void collideSegments(bool finalPass);
        void dampSupportedMotion(float dt);
        void evaluateSleep(float dt);
        void applyPose();
        void syncSimulationToPose();
        osg::Matrixf getWorldMatrix(osg::MatrixTransform* node) const;

        MWWorld::Ptr mActor;
        osg::observer_ptr<MWRender::Animation> mAnimation;
        const RayCastingInterface* mRayCasting;
        std::vector<Bone> mBones;
        bool mValid;
        bool mSleeping;
        float mSleepTimer;
        float mAccumulator;
    };
}

#endif
