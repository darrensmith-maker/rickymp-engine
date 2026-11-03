#include "ragdoll.hpp"

#include <algorithm>
#include <cmath>

#include <osg/MatrixTransform>
#include <osg/Node>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwrender/animation.hpp"
#include "raycasting.hpp"

namespace
{
    constexpr float sGravity = 627.2f;
    // A 120 Hz ragdoll step greatly reduces tunnelling of the relatively thin
    // Morrowind limbs without forcing the rest of OpenMW's physics to run at 120 Hz.
    constexpr float sFixedStep = 1.f / 120.f;
    constexpr float sAirDamping = 0.992f;
    constexpr float sContactDamping = 0.88f;
    constexpr float sBounce = 0.018f;
    constexpr float sSurfaceFriction = 0.64f;
    constexpr float sStaticFriction = 0.72f;
    constexpr float sContactSkin = 0.28f;
    constexpr int sPositionIterations = 12;
    constexpr int sShockIterations = 4;

    float clampRadius(float value)
    {
        return std::max(3.5f, std::min(13.f, value));
    }

    // Approximate human segment masses. The exact kilograms are less important
    // than keeping the centre of mass in pelvis/torso rather than wrists/ankles.
    float massForBone(const std::string& name)
    {
        if (name == "Bip01 Pelvis")
            return 13.f;
        if (name == "Bip01 Spine1")
            return 19.f;
        if (name == "Bip01 Head")
            return 5.f;
        if (name.find("UpperArm") != std::string::npos)
            return 2.4f;
        if (name.find("Forearm") != std::string::npos)
            return 1.7f;
        if (name.find("Hand") != std::string::npos)
            return 0.7f;
        if (name.find("Thigh") != std::string::npos)
            return 7.6f;
        if (name.find("Calf") != std::string::npos)
            return 3.7f;
        if (name.find("Foot") != std::string::npos)
            return 1.1f;
        return 3.f;
    }

    float inverseMass(float mass)
    {
        return 1.f / std::max(0.1f, mass);
    }

    // Modern joint solvers commonly use effective mass/inertia scaling so that a
    // tiny child does not make a heavy parent-child chain converge poorly. Clamp
    // the ratio used by the *constraint* solve while retaining the real mass for
    // centre-of-mass and sleep calculations.
    void equalizeConstraintMass(float& first, float& second)
    {
        constexpr float maxRatio = 3.f;
        if (first > second * maxRatio)
            first = second * maxRatio;
        if (second > first * maxRatio)
            second = first * maxRatio;
    }

    osg::Vec3f safeNormalized(const osg::Vec3f& value, const osg::Vec3f& fallback)
    {
        const float len2 = value.length2();
        if (len2 < 0.0001f)
            return fallback;
        return value / std::sqrt(len2);
    }

    float clampf(float value, float minimum, float maximum)
    {
        return std::max(minimum, std::min(maximum, value));
    }

    float radians(float degrees)
    {
        return degrees * 3.14159265358979323846f / 180.f;
    }
}

namespace MWPhysics
{
    Ragdoll::Ragdoll(const MWWorld::Ptr& actor, MWRender::Animation* animation,
        const RayCastingInterface* rayCasting)
        : mActor(actor)
        , mAnimation(animation)
        , mRayCasting(rayCasting)
        , mValid(false)
        , mSleeping(false)
        , mSleepTimer(0.f)
        , mAccumulator(0.f)
    {
    }

    Ragdoll::~Ragdoll()
    {
        stop();
    }

    osg::Matrixf Ragdoll::getWorldMatrix(osg::MatrixTransform* node) const
    {
        if (!node)
            return osg::Matrixf();

        const osg::NodePathList paths = node->getParentalNodePaths();
        if (paths.empty())
            return node->getMatrix();
        return osg::computeLocalToWorld(paths.front());
    }

    bool Ragdoll::addBone(const char* name, const char* parent)
    {
        osg::MatrixTransform* node = mAnimation.valid() ? mAnimation->getNodeTransform(name) : nullptr;
        if (!node)
            return false;

        Bone bone;
        bone.mName = name;
        bone.mParentName = parent ? parent : "";
        bone.mNode = node;

        const osg::Matrixf world = getWorldMatrix(node);
        bone.mPosition = world.getTrans();
        bone.mPreviousPosition = bone.mPosition;
        bone.mInitialPosition = bone.mPosition;
        bone.mInitialWorldRotation = world.getRotate();
        bone.mInitialLocalMatrix = node->getMatrix();
        bone.mInitialLocalTranslation = bone.mInitialLocalMatrix.getTrans();
        bone.mMass = massForBone(bone.mName);
        mBones.push_back(std::move(bone));
        return true;
    }

    void Ragdoll::resolveHierarchy()
    {
        for (std::size_t i = 0; i < mBones.size(); ++i)
        {
            Bone& bone = mBones[i];
            if (bone.mParentName.empty())
                continue;

            for (std::size_t p = 0; p < mBones.size(); ++p)
            {
                if (mBones[p].mName == bone.mParentName)
                {
                    bone.mParent = static_cast<int>(p);
                    const float length = (bone.mInitialPosition - mBones[p].mInitialPosition).length();
                    bone.mRestLength = std::max(1.f, length);
                    bone.mRadius = clampRadius(bone.mRestLength * 0.20f);
                    if (mBones[p].mDirectionChild == -1)
                        mBones[p].mDirectionChild = static_cast<int>(i);
                    break;
                }
            }
        }

        // Contact volumes: intentionally a little fatter than the old prototype.
        // Thin endpoints were the part most likely to tunnel through table edges.
        for (Bone& bone : mBones)
        {
            if (bone.mName == "Bip01 Pelvis")
                bone.mRadius = 14.f;
            else if (bone.mName == "Bip01 Spine1")
                bone.mRadius = 13.5f;
            else if (bone.mName == "Bip01 Head")
                bone.mRadius = 9.5f;
            else if (bone.mName.find("Hand") != std::string::npos || bone.mName.find("Foot") != std::string::npos)
                bone.mRadius = 5.f;
            else
                bone.mRadius = std::max(4.5f, std::min(9.f, bone.mRadius));
        }

        const auto setDirectionChild = [this](const char* parent, const char* child)
        {
            int parentIndex = -1;
            int childIndex = -1;
            for (std::size_t i = 0; i < mBones.size(); ++i)
            {
                if (mBones[i].mName == parent)
                    parentIndex = static_cast<int>(i);
                if (mBones[i].mName == child)
                    childIndex = static_cast<int>(i);
            }
            if (parentIndex >= 0 && childIndex >= 0)
                mBones[parentIndex].mDirectionChild = childIndex;
        };

        setDirectionChild("Bip01 Pelvis", "Bip01 Spine1");
        setDirectionChild("Bip01 Spine1", "Bip01 Head");
        setDirectionChild("Bip01 L UpperArm", "Bip01 L Forearm");
        setDirectionChild("Bip01 L Forearm", "Bip01 L Hand");
        setDirectionChild("Bip01 R UpperArm", "Bip01 R Forearm");
        setDirectionChild("Bip01 R Forearm", "Bip01 R Hand");
        setDirectionChild("Bip01 L Thigh", "Bip01 L Calf");
        setDirectionChild("Bip01 L Calf", "Bip01 L Foot");
        setDirectionChild("Bip01 R Thigh", "Bip01 R Calf");
        setDirectionChild("Bip01 R Calf", "Bip01 R Foot");
    }

    void Ragdoll::seedVelocity()
    {
        osg::Vec3f direction(0.f, 0.f, 0.f);
        MWBase::World* world = MWBase::Environment::get().getWorld();
        if (world && !mActor.isEmpty())
        {
            const MWWorld::Ptr player = world->getPlayerPtr();
            if (!player.isEmpty() && player != mActor)
                direction = mActor.getRefData().getPosition().asVec3()
                    - player.getRefData().getPosition().asVec3();
        }

        direction.z() = 0.f;
        direction = safeNormalized(direction, osg::Vec3f(0.f, 1.f, 0.f));
        // Less artificial launch than the prototype. The body starts with enough
        // momentum to react to death, but gravity and the captured pose dominate.
        const osg::Vec3f initialVelocity = direction * 58.f + osg::Vec3f(0.f, 0.f, 24.f);

        for (Bone& bone : mBones)
            bone.mPreviousPosition = bone.mPosition - initialVelocity * sFixedStep;
    }

    bool Ragdoll::initialize()
    {
        if (mValid)
            return true;
        if (mActor.isEmpty() || !mAnimation.valid() || !mRayCasting)
            return false;

        mBones.clear();

        const bool pelvis = addBone("Bip01 Pelvis", nullptr);
        const bool spine = addBone("Bip01 Spine1", "Bip01 Pelvis");
        addBone("Bip01 Head", "Bip01 Spine1");
        addBone("Bip01 L UpperArm", "Bip01 Spine1");
        addBone("Bip01 L Forearm", "Bip01 L UpperArm");
        addBone("Bip01 L Hand", "Bip01 L Forearm");
        addBone("Bip01 R UpperArm", "Bip01 Spine1");
        addBone("Bip01 R Forearm", "Bip01 R UpperArm");
        addBone("Bip01 R Hand", "Bip01 R Forearm");
        addBone("Bip01 L Thigh", "Bip01 Pelvis");
        addBone("Bip01 L Calf", "Bip01 L Thigh");
        addBone("Bip01 L Foot", "Bip01 L Calf");
        addBone("Bip01 R Thigh", "Bip01 Pelvis");
        addBone("Bip01 R Calf", "Bip01 R Thigh");
        addBone("Bip01 R Foot", "Bip01 R Calf");

        if (!pelvis || !spine || mBones.size() < 6)
        {
            mBones.clear();
            return false;
        }

        resolveHierarchy();
        seedVelocity();
        mAnimation->setRagdollMode(true);
        mAnimation->markSkeletonDirty();

        mSleeping = false;
        mSleepTimer = 0.f;
        mAccumulator = 0.f;
        mValid = true;
        return true;
    }

    void Ragdoll::solveConstraints(int iterations, bool shockPropagation)
    {
        for (int iteration = 0; iteration < iterations; ++iteration)
        {
            for (std::size_t i = 0; i < mBones.size(); ++i)
            {
                Bone& child = mBones[i];
                if (child.mParent < 0 || child.mRestLength <= 0.f)
                    continue;

                Bone& parent = mBones[static_cast<std::size_t>(child.mParent)];
                osg::Vec3f delta = child.mPosition - parent.mPosition;
                const float length = delta.length();
                if (length < 0.001f)
                    continue;

                const float error = (length - child.mRestLength) / length;
                const osg::Vec3f correction = delta * error;

                float parentInv = inverseMass(parent.mMass);
                float childInv = inverseMass(child.mMass);
                equalizeConstraintMass(parentInv, childInv);

                // Shock propagation: in the last passes make the parent side of
                // each chain temporarily heavier. This stiffens pelvis->torso and
                // torso->limb chains without changing their actual COM weighting.
                if (shockPropagation)
                    parentInv *= 0.22f;

                const float weightSum = std::max(0.00001f, parentInv + childInv);
                parent.mPosition += correction * (parentInv / weightSum);
                child.mPosition -= correction * (childInv / weightSum);
            }
        }
    }

    void Ragdoll::solveJointLimits()
    {
        const auto findBone = [this](const char* name) -> int
        {
            for (std::size_t i = 0; i < mBones.size(); ++i)
                if (mBones[i].mName == name)
                    return static_cast<int>(i);
            return -1;
        };

        const auto constrainDistance = [this](int a, int b, float minimum, float maximum, float stiffness)
        {
            if (a < 0 || b < 0)
                return;
            Bone& first = mBones[static_cast<std::size_t>(a)];
            Bone& second = mBones[static_cast<std::size_t>(b)];
            osg::Vec3f delta = second.mPosition - first.mPosition;
            const float distance = delta.length();
            if (distance < 0.001f)
                return;

            float target = distance;
            if (distance < minimum)
                target = minimum;
            else if (distance > maximum)
                target = maximum;
            else
                return;

            const osg::Vec3f correction = delta * ((distance - target) / distance) * stiffness;
            float firstInv = inverseMass(first.mMass);
            float secondInv = inverseMass(second.mMass);
            equalizeConstraintMass(firstInv, secondInv);
            const float weightSum = std::max(0.00001f, firstInv + secondInv);
            first.mPosition += correction * (firstInv / weightSum);
            second.mPosition -= correction * (secondInv / weightSum);
        };

        const auto preserveInitialDistance = [&](const char* aName, const char* bName,
            float minScale, float maxScale, float stiffness)
        {
            const int a = findBone(aName);
            const int b = findBone(bName);
            if (a < 0 || b < 0)
                return;
            const float rest = (mBones[a].mInitialPosition - mBones[b].mInitialPosition).length();
            constrainDistance(a, b, rest * minScale, rest * maxScale, stiffness);
        };

        // Only rigidify the two broad body widths. The old diagonal web across
        // pelvis/chest over-constrained the body and could pull limbs through a
        // table while trying to satisfy every distance at once.
        preserveInitialDistance("Bip01 L UpperArm", "Bip01 R UpperArm", 0.94f, 1.06f, 0.72f);
        preserveInitialDistance("Bip01 L Thigh", "Bip01 R Thigh", 0.94f, 1.06f, 0.78f);

        const auto limitBend = [&](const char* rootName, const char* jointName,
            const char* endName, float minimumAngleDegrees)
        {
            const int root = findBone(rootName);
            const int joint = findBone(jointName);
            const int end = findBone(endName);
            if (root < 0 || joint < 0 || end < 0)
                return;

            Bone& rootBone = mBones[static_cast<std::size_t>(root)];
            Bone& jointBone = mBones[static_cast<std::size_t>(joint)];
            Bone& endBone = mBones[static_cast<std::size_t>(end)];

            const float firstLength = (jointBone.mInitialPosition - rootBone.mInitialPosition).length();
            const float secondLength = (endBone.mInitialPosition - jointBone.mInitialPosition).length();
            if (firstLength < 0.001f || secondLength < 0.001f)
                return;

            // Angle at the joint: 180 degrees is straight. Enforce a minimum
            // anatomical opening angle so elbows/knees cannot fold back through
            // themselves. Convert that angular limit to a root-end distance.
            const float minimumAngle = radians(minimumAngleDegrees);
            const float minEndDistance = std::sqrt(std::max(0.f,
                firstLength * firstLength + secondLength * secondLength
                - 2.f * firstLength * secondLength * std::cos(minimumAngle)));
            constrainDistance(root, end, minEndDistance,
                (firstLength + secondLength) * 1.01f, 0.92f);

            // Prevent the hinge from flipping to the opposite side of its bend
            // plane. Rotate the death-pose plane with the current upper segment,
            // then compare the lower segment's current plane against it.
            osg::Vec3f initialUpper = jointBone.mInitialPosition - rootBone.mInitialPosition;
            osg::Vec3f initialLower = endBone.mInitialPosition - jointBone.mInitialPosition;
            osg::Vec3f initialNormal = initialUpper ^ initialLower;
            if (initialNormal.length2() < 0.01f)
                return; // nearly straight pose: angle limit alone is safer

            initialUpper.normalize();
            initialNormal.normalize();
            const osg::Vec3f currentUpper = safeNormalized(
                jointBone.mPosition - rootBone.mPosition, initialUpper);
            osg::Quat upperAlignment;
            upperAlignment.makeRotate(initialUpper, currentUpper);
            osg::Vec3f allowedNormal = upperAlignment * initialNormal;
            allowedNormal = safeNormalized(allowedNormal, initialNormal);

            osg::Vec3f currentLower = endBone.mPosition - jointBone.mPosition;
            const osg::Vec3f currentPlane = currentUpper ^ currentLower;
            if (currentPlane.length2() > 0.001f && currentPlane * allowedNormal < -0.12f)
            {
                // Reflect the lower vector through the allowed bend plane. This
                // is a projection, not a spring: impossible inverted knees are
                // corrected immediately instead of accumulating solver energy.
                const float normalComponent = currentLower * allowedNormal;
                currentLower -= allowedNormal * (2.f * normalComponent);
                currentLower = safeNormalized(currentLower,
                    endBone.mInitialPosition - jointBone.mInitialPosition) * secondLength;
                endBone.mPosition = jointBone.mPosition + currentLower;
            }
        };

        // Conservative game-ragdoll limits. They allow a relaxed collapsed pose
        // while stopping the grotesque full inversion visible in the old build.
        limitBend("Bip01 L UpperArm", "Bip01 L Forearm", "Bip01 L Hand", 28.f);
        limitBend("Bip01 R UpperArm", "Bip01 R Forearm", "Bip01 R Hand", 28.f);
        limitBend("Bip01 L Thigh", "Bip01 L Calf", "Bip01 L Foot", 24.f);
        limitBend("Bip01 R Thigh", "Bip01 R Calf", "Bip01 R Foot", 24.f);
    }

    void Ragdoll::solveSelfCollisions()
    {
        const auto sameChainNear = [this](std::size_t aIndex, std::size_t bIndex)
        {
            const Bone& a = mBones[aIndex];
            const Bone& b = mBones[bIndex];
            if (a.mParent == static_cast<int>(bIndex) || b.mParent == static_cast<int>(aIndex))
                return true;
            if (a.mParent >= 0 && mBones[static_cast<std::size_t>(a.mParent)].mParent == static_cast<int>(bIndex))
                return true;
            if (b.mParent >= 0 && mBones[static_cast<std::size_t>(b.mParent)].mParent == static_cast<int>(aIndex))
                return true;
            return false;
        };

        for (std::size_t i = 0; i < mBones.size(); ++i)
        {
            for (std::size_t j = i + 1; j < mBones.size(); ++j)
            {
                Bone& a = mBones[i];
                Bone& b = mBones[j];
                // Adjacent and next-neighbour links in one limb do not collide in
                // modern ragdolls; making a constrained chain fight itself is a
                // common source of explosive jitter.
                if (sameChainNear(i, j))
                    continue;

                const float initialDistance = (a.mInitialPosition - b.mInitialPosition).length();
                const float minimum = std::min((a.mRadius + b.mRadius) * 0.82f,
                    initialDistance * 0.68f);
                if (minimum <= 0.01f)
                    continue;

                osg::Vec3f delta = b.mPosition - a.mPosition;
                const float distance = delta.length();
                if (distance >= minimum)
                    continue;

                const osg::Vec3f direction = distance > 0.001f
                    ? delta / distance
                    : safeNormalized(b.mInitialPosition - a.mInitialPosition, osg::Vec3f(1.f, 0.f, 0.f));
                const osg::Vec3f correction = direction * (minimum - distance) * 0.78f;

                float aInv = inverseMass(a.mMass);
                float bInv = inverseMass(b.mMass);
                equalizeConstraintMass(aInv, bInv);
                const float weightSum = std::max(0.00001f, aInv + bInv);
                a.mPosition -= correction * (aInv / weightSum);
                b.mPosition += correction * (bInv / weightSum);
            }
        }
    }

    void Ragdoll::resetContacts()
    {
        for (Bone& bone : mBones)
        {
            bone.mContacted = false;
            bone.mSupported = false;
            bone.mContactNormal.set(0.f, 0.f, 1.f);
        }
    }

    void Ragdoll::collideBones(bool finalPass)
    {
        const osg::Quat identity;
        for (Bone& bone : mBones)
        {
            osg::Vec3f velocity = bone.mPosition - bone.mPreviousPosition;
            const osg::Vec3f travel = velocity;

            if (travel.length2() > 0.000001f)
            {
                const RayCastingResult hit = mRayCasting->castSphere(
                    bone.mPreviousPosition, bone.mPosition, bone.mRadius, mActor);
                if (hit.mHit)
                {
                    const osg::Vec3f normal = safeNormalized(hit.mHitNormal, osg::Vec3f(0.f, 0.f, 1.f));
                    const float fraction = clampf(hit.mHitFraction - 0.004f, 0.f, 1.f);
                    bone.mPosition = bone.mPreviousPosition + travel * fraction + normal * sContactSkin;

                    const float normalVelocity = velocity * normal;
                    if (normalVelocity < 0.f)
                    {
                        const float restitution = (-normalVelocity > 90.f) ? sBounce : 0.f;
                        velocity -= normal * ((1.f + restitution) * normalVelocity);
                    }

                    const osg::Vec3f normalPart = normal * std::max(0.f, velocity * normal);
                    osg::Vec3f tangentPart = velocity - normal * (velocity * normal);
                    const osg::Vec3f tangentGravity = osg::Vec3f(0.f, 0.f, -sGravity)
                        - normal * (osg::Vec3f(0.f, 0.f, -sGravity) * normal);
                    const float tangentGravityMagnitude = tangentGravity.length();
                    const float normalGravityMagnitude = sGravity * std::max(0.f, normal.z());
                    const bool stableSlope = tangentGravityMagnitude
                        <= sStaticFriction * normalGravityMagnitude;

                    if (stableSlope && tangentPart.length() / sFixedStep < 58.f)
                        tangentPart.set(0.f, 0.f, 0.f);
                    else
                        tangentPart *= sSurfaceFriction;

                    velocity = normalPart + tangentPart;
                    bone.mPreviousPosition = bone.mPosition - velocity;
                    bone.mContacted = true;
                    bone.mSupported = normal.z() > 0.45f;
                    bone.mContactNormal = normal;
                }
            }

            // Constraint projection can move a joint directly into static geometry
            // without crossing the boundary. A final overlap recovery is therefore
            // required in addition to CCD/sweeps.
            const float boxRadius = bone.mRadius * 0.88f;
            const osg::Vec3f correction = mRayCasting->getBoxPenetrationCorrection(
                bone.mPosition, identity, osg::Vec3f(boxRadius, boxRadius, boxRadius),
                mActor, finalPass ? 5.5f : 2.5f);
            if (correction.length2() > 0.000001f)
            {
                osg::Vec3f normal = safeNormalized(correction, osg::Vec3f(0.f, 0.f, 1.f));
                velocity = bone.mPosition - bone.mPreviousPosition;
                bone.mPosition += correction;
                const float inward = velocity * normal;
                if (inward < 0.f)
                    velocity -= normal * inward;
                velocity *= finalPass ? 0.58f : 0.72f;
                bone.mPreviousPosition = bone.mPosition - velocity;
                bone.mContacted = true;
                bone.mSupported = bone.mSupported || normal.z() > 0.45f;
                bone.mContactNormal = normal;
            }
        }
    }

    void Ragdoll::collideSegments(bool finalPass)
    {
        for (std::size_t i = 0; i < mBones.size(); ++i)
        {
            Bone& child = mBones[i];
            if (child.mParent < 0 || child.mRestLength <= 1.f)
                continue;

            Bone& parent = mBones[static_cast<std::size_t>(child.mParent)];
            const osg::Vec3f previousDirection = child.mPreviousPosition - parent.mPreviousPosition;
            const osg::Vec3f currentDirection = child.mPosition - parent.mPosition;
            if (previousDirection.length2() < 0.001f || currentDirection.length2() < 0.001f)
                continue;

            const osg::Vec3f previousMid = (child.mPreviousPosition + parent.mPreviousPosition) * 0.5f;
            const osg::Vec3f currentMid = (child.mPosition + parent.mPosition) * 0.5f;

            osg::Quat previousRotation;
            previousRotation.makeRotate(osg::Vec3f(0.f, 1.f, 0.f),
                safeNormalized(previousDirection, osg::Vec3f(0.f, 1.f, 0.f)));
            osg::Quat currentRotation;
            currentRotation.makeRotate(osg::Vec3f(0.f, 1.f, 0.f),
                safeNormalized(currentDirection, osg::Vec3f(0.f, 1.f, 0.f)));

            const float radius = std::max(3.2f, std::min(parent.mRadius, child.mRadius) * 0.76f);
            const osg::Vec3f halfExtents(radius, std::max(2.f, child.mRestLength * 0.48f), radius);
            const RayCastingResult hit = mRayCasting->castBox(
                previousMid, previousRotation, currentMid, currentRotation, halfExtents, mActor);

            if (hit.mHit)
            {
                const osg::Vec3f normal = safeNormalized(hit.mHitNormal, osg::Vec3f(0.f, 0.f, 1.f));
                const float fraction = clampf(hit.mHitFraction - 0.006f, 0.f, 1.f);
                const osg::Vec3f parentTravel = parent.mPosition - parent.mPreviousPosition;
                const osg::Vec3f childTravel = child.mPosition - child.mPreviousPosition;
                parent.mPosition = parent.mPreviousPosition + parentTravel * fraction + normal * sContactSkin;
                child.mPosition = child.mPreviousPosition + childTravel * fraction + normal * sContactSkin;

                auto projectVelocity = [&](Bone& bone)
                {
                    osg::Vec3f v = bone.mPosition - bone.mPreviousPosition;
                    const float inward = v * normal;
                    if (inward < 0.f)
                        v -= normal * inward;
                    v -= normal * (v * normal);
                    v *= sSurfaceFriction;
                    bone.mPreviousPosition = bone.mPosition - v;
                    bone.mContacted = true;
                    bone.mSupported = bone.mSupported || normal.z() > 0.45f;
                    bone.mContactNormal = normal;
                };
                projectVelocity(parent);
                projectVelocity(child);
            }

            // Also solve an already-overlapping limb volume. This is the key fix
            // for bodies getting wedged through furniture after a joint projection.
            const osg::Vec3f correctedDirection = child.mPosition - parent.mPosition;
            if (correctedDirection.length2() < 0.001f)
                continue;
            const osg::Vec3f correctedMid = (child.mPosition + parent.mPosition) * 0.5f;
            osg::Quat correctedRotation;
            correctedRotation.makeRotate(osg::Vec3f(0.f, 1.f, 0.f),
                safeNormalized(correctedDirection, osg::Vec3f(0.f, 1.f, 0.f)));
            const osg::Vec3f correction = mRayCasting->getBoxPenetrationCorrection(
                correctedMid, correctedRotation, halfExtents, mActor, finalPass ? 5.f : 2.f);
            if (correction.length2() > 0.000001f)
            {
                float parentInv = inverseMass(parent.mMass);
                float childInv = inverseMass(child.mMass);
                equalizeConstraintMass(parentInv, childInv);
                const float sum = std::max(0.00001f, parentInv + childInv);
                parent.mPosition += correction * (parentInv / sum);
                child.mPosition += correction * (childInv / sum);

                const osg::Vec3f normal = safeNormalized(correction, osg::Vec3f(0.f, 0.f, 1.f));
                for (Bone* bone : { &parent, &child })
                {
                    osg::Vec3f v = bone->mPosition - bone->mPreviousPosition;
                    const float inward = v * normal;
                    if (inward < 0.f)
                        v -= normal * inward;
                    v *= finalPass ? 0.60f : 0.76f;
                    bone->mPreviousPosition = bone->mPosition - v;
                    bone->mContacted = true;
                    bone->mSupported = bone->mSupported || normal.z() > 0.45f;
                    bone->mContactNormal = normal;
                }
            }
        }
    }

    void Ragdoll::projectWorldContacts(bool finalPass)
    {
        collideBones(finalPass);
        collideSegments(finalPass);
    }

    void Ragdoll::dampSupportedMotion(float dt)
    {
        int supported = 0;
        for (const Bone& bone : mBones)
            supported += bone.mSupported ? 1 : 0;

        const float supportRatio = mBones.empty() ? 0.f
            : static_cast<float>(supported) / static_cast<float>(mBones.size());
        if (supportRatio <= 0.f)
            return;

        for (Bone& bone : mBones)
        {
            osg::Vec3f velocity = bone.mPosition - bone.mPreviousPosition;
            const float speed = velocity.length() / std::max(dt, 0.0001f);

            if (bone.mSupported)
            {
                const osg::Vec3f normal = safeNormalized(bone.mContactNormal, osg::Vec3f(0.f, 0.f, 1.f));
                osg::Vec3f tangent = velocity - normal * (velocity * normal);
                const float tangentSpeed = tangent.length() / std::max(dt, 0.0001f);
                if (normal.z() > 0.55f && tangentSpeed < 44.f)
                    tangent.set(0.f, 0.f, 0.f);
                else
                    tangent *= 0.76f;
                const osg::Vec3f outward = normal * std::max(0.f, velocity * normal);
                velocity = outward + tangent;
            }

            // Once most of the body is supported, use strong low-speed damping.
            // This emulates the stabilization/sleep phase of game rigid-body
            // engines instead of letting tiny contact impulses keep the corpse alive.
            if (supportRatio >= 0.30f && speed < 95.f)
                velocity *= sContactDamping;
            bone.mPreviousPosition = bone.mPosition - velocity;
        }
    }

    void Ragdoll::evaluateSleep(float dt)
    {
        float massSum = 0.f;
        float energySpeedSq = 0.f;
        float maxSpeed = 0.f;
        int supported = 0;
        float pelvisSpeed = 0.f;

        for (const Bone& bone : mBones)
        {
            const float speed = (bone.mPosition - bone.mPreviousPosition).length()
                / std::max(dt, 0.0001f);
            massSum += bone.mMass;
            energySpeedSq += bone.mMass * speed * speed;
            maxSpeed = std::max(maxSpeed, speed);
            supported += bone.mSupported ? 1 : 0;
            if (bone.mName == "Bip01 Pelvis")
                pelvisSpeed = speed;
        }

        const float rmsSpeed = massSum > 0.f ? std::sqrt(energySpeedSq / massSum) : 0.f;
        const float supportRatio = mBones.empty() ? 0.f
            : static_cast<float>(supported) / static_cast<float>(mBones.size());

        // Whole-body energy is a better sleep signal than the noisiest fingertip.
        // A supported corpse settles quickly; an airborne/falling corpse never sleeps.
        const bool settled = supportRatio >= 0.20f && rmsSpeed < 18.f
            && maxSpeed < 48.f && pelvisSpeed < 28.f;
        const bool verySettled = supportRatio >= 0.33f && rmsSpeed < 10.f
            && maxSpeed < 30.f && pelvisSpeed < 16.f;

        if (verySettled)
            mSleepTimer += dt * 2.6f;
        else if (settled)
            mSleepTimer += dt;
        else
            mSleepTimer = std::max(0.f, mSleepTimer - dt * 2.f);

        if (mSleepTimer >= 0.42f)
        {
            mSleeping = true;
            for (Bone& bone : mBones)
                bone.mPreviousPosition = bone.mPosition;
        }
    }

    void Ragdoll::simulateStep(float dt)
    {
        resetContacts();

        for (Bone& bone : mBones)
        {
            osg::Vec3f velocity = (bone.mPosition - bone.mPreviousPosition) * sAirDamping;
            // Limit one substep's ballistic displacement. This is a visual-game
            // ragdoll safety clamp, analogous to limiting depenetration/contact
            // velocities in rigid-body engines.
            const float maxStep = std::max(7.f, bone.mRadius * 0.85f);
            if (velocity.length() > maxStep)
                velocity *= maxStep / velocity.length();

            bone.mPreviousPosition = bone.mPosition;
            bone.mPosition += velocity + osg::Vec3f(0.f, 0.f, -sGravity * dt * dt);
        }

        // Position solve -> contacts -> final stiffening -> contacts. Crucially,
        // there is no naked joint solve after the final contact projection, which
        // was the old path that could push a corrected limb straight back into a wall.
        solveConstraints(sPositionIterations, false);
        for (int i = 0; i < 3; ++i)
        {
            solveJointLimits();
            solveSelfCollisions();
        }
        projectWorldContacts(false);

        solveConstraints(sShockIterations, true);
        solveJointLimits();
        projectWorldContacts(true);

        dampSupportedMotion(dt);
        evaluateSleep(dt);
    }

    void Ragdoll::applyPose()
    {
        if (!mAnimation.valid())
            return;

        for (std::size_t i = 0; i < mBones.size(); ++i)
        {
            Bone& bone = mBones[i];
            osg::MatrixTransform* node = bone.mNode.get();
            if (!node)
                continue;

            osg::Vec3f initialDirection(0.f, 1.f, 0.f);
            osg::Vec3f currentDirection = initialDirection;

            if (bone.mDirectionChild >= 0)
            {
                const Bone& child = mBones[static_cast<std::size_t>(bone.mDirectionChild)];
                initialDirection = child.mInitialPosition - bone.mInitialPosition;
                currentDirection = child.mPosition - bone.mPosition;
            }
            else if (bone.mParent >= 0)
            {
                const Bone& parent = mBones[static_cast<std::size_t>(bone.mParent)];
                initialDirection = bone.mInitialPosition - parent.mInitialPosition;
                currentDirection = bone.mPosition - parent.mPosition;
            }

            initialDirection = safeNormalized(initialDirection, osg::Vec3f(0.f, 1.f, 0.f));
            currentDirection = safeNormalized(currentDirection, initialDirection);

            osg::Quat alignment;
            alignment.makeRotate(initialDirection, currentDirection);
            const osg::Quat desiredWorldRotation = alignment * bone.mInitialWorldRotation;

            osg::NodePathList paths = node->getParentalNodePaths();
            if (paths.empty())
                continue;

            osg::NodePath parentPath = paths.front();
            if (!parentPath.empty() && parentPath.back() == node)
                parentPath.pop_back();

            const osg::Matrixf parentWorld = osg::computeLocalToWorld(parentPath);
            const osg::Quat desiredLocalRotation = desiredWorldRotation * parentWorld.getRotate().inverse();

            if (bone.mParent < 0)
            {
                osg::Matrixf desiredWorld = osg::Matrixf::rotate(desiredWorldRotation);
                desiredWorld.setTrans(bone.mPosition);
                osg::Matrixf inverseParent;
                inverseParent.invert(parentWorld);
                node->setMatrix(desiredWorld * inverseParent);
            }
            else
            {
                // Never write simulated translation into child bones. The mesh keeps
                // authored limb lengths; physics contributes orientation only.
                osg::Matrixf local = bone.mInitialLocalMatrix;
                local.setRotate(desiredLocalRotation);
                local.setTrans(bone.mInitialLocalTranslation);
                node->setMatrix(local);
            }
        }

        mAnimation->markSkeletonDirty();
        syncSimulationToPose();
    }

    void Ragdoll::syncSimulationToPose()
    {
        for (Bone& bone : mBones)
        {
            osg::MatrixTransform* node = bone.mNode.get();
            if (!node)
                continue;

            osg::Vec3f velocity = bone.mPosition - bone.mPreviousPosition;
            const float maxStepDisplacement = std::max(3.8f, bone.mRadius * 0.58f);
            const float speedStep = velocity.length();
            if (speedStep > maxStepDisplacement)
                velocity *= maxStepDisplacement / speedStep;

            const osg::Vec3f visualPosition = getWorldMatrix(node).getTrans();
            bone.mPosition = visualPosition;
            bone.mPreviousPosition = visualPosition - velocity;
        }
    }

    void Ragdoll::update(float dt)
    {
        if (!mValid || !mAnimation.valid() || mActor.isEmpty())
            return;

        dt = clampf(dt, 0.f, 0.08f);
        mAccumulator += dt;

        if (!mSleeping)
        {
            int steps = 0;
            while (mAccumulator >= sFixedStep && steps < 10)
            {
                simulateStep(sFixedStep);
                mAccumulator -= sFixedStep;
                ++steps;
            }
            if (steps == 10 && mAccumulator > sFixedStep * 2.f)
                mAccumulator = sFixedStep * 2.f;
        }
        else
            mAccumulator = 0.f;

        applyPose();
    }

    void Ragdoll::stop()
    {
        if (mAnimation.valid() && mAnimation->isRagdollMode())
            mAnimation->setRagdollMode(false);
        mBones.clear();
        mValid = false;
        mSleeping = false;
        mSleepTimer = 0.f;
        mAccumulator = 0.f;
    }
}
