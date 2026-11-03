#include "camera.hpp"

#include <algorithm>
#include <cmath>

#include <osg/Camera>

#include <components/misc/mathutil.hpp>
#include <components/sceneutil/positionattitudetransform.hpp>
#include <components/settings/settings.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/ptr.hpp"
#include "../mwworld/refdata.hpp"

#include "../mwmechanics/drawstate.hpp"
#include "../mwmechanics/movement.hpp"
#include "../mwmechanics/npcstats.hpp"

#include "../mwphysics/raycasting.hpp"

#include "npcanimation.hpp"

namespace
{

class UpdateRenderCameraCallback : public osg::NodeCallback
{
public:
    UpdateRenderCameraCallback(MWRender::Camera* cam)
        : mCamera(cam)
    {
    }

    void operator()(osg::Node* node, osg::NodeVisitor* nv) override
    {
        osg::Camera* cam = static_cast<osg::Camera*>(node);

        // traverse first to update animations, in case the camera is attached to an animated node
        traverse(node, nv);

        mCamera->updateCamera(cam);
    }

private:
    MWRender::Camera* mCamera;
};

}

namespace MWRender
{

    Camera::Camera (osg::Camera* camera)
    : mDialogueCameraActive(false),
      mDialogueViewInitialized(false),
      mDialogueCurrentPosition(osg::Vec3d()),
      mDialogueCurrentLookAt(osg::Vec3d()),
      mHeightScale(1.f),
      mCamera(camera),
      mAnimation(nullptr),
      mFirstPersonView(true),
      mMode(Mode::Normal),
      mVanityAllowed(true),
      mStandingPreviewAllowed(Settings::Manager::getBool("preview if stand still", "Camera")),
      mDeferredRotationAllowed(Settings::Manager::getBool("deferred preview rotation", "Camera")),
      mNearest(30.f),
      mFurthest(800.f),
      mIsNearest(false),
      mHeight(124.f),
      mBaseCameraDistance(Settings::Manager::getFloat("third person camera distance", "Camera")),
      mPitch(0.f),
      mYaw(0.f),
      mRoll(0.f),
      mVanityToggleQueued(false),
      mVanityToggleQueuedValue(false),
      mViewModeToggleQueued(false),
      mViewTransitionActive(false),
      mViewTransitionTargetFirstPerson(true),
      mViewTransitionViewModeApplied(true),
      mViewTransitionElapsed(0.f),
      mViewTransitionDuration(0.32f),
      mViewTransitionStartPosition(osg::Vec3d()),
      mViewTransitionStartLookAt(osg::Vec3d()),
      mViewTransitionStartUp(0.0, 0.0, 1.0),
      mCameraDistance(0.f),
      mMaxNextCameraDistance(800.f),
      mFocalPointCurrentOffset(osg::Vec2d()),
      mFocalPointTargetOffset(osg::Vec2d()),
      mFocalPointTransitionSpeedCoef(1.f),
      mSkipFocalPointTransition(true),
      mPreviousTransitionInfluence(0.f),
      mSmoothedSpeed(0.f),
      mZoomOutWhenMoveCoef(Settings::Manager::getFloat("zoom out when move coef", "Camera")),
      mDynamicCameraDistanceEnabled(false),
      mShowCrosshairInThirdPersonMode(false),
      mHeadBobbingEnabled(Settings::Manager::getBool("head bobbing", "Camera")),
      mImmersiveFirstPersonEnabled(Settings::Manager::getBool("immersive first person", "Camera")),
      mImmersiveFirstPersonForwardOffset(
          Settings::Manager::getFloat("immersive first person forward offset", "Camera")),
      mHeadBobbingOffset(0.f),
      mHeadBobbingRoll(0.f),
      mHeadBobbingWeight(0.f),
      mTotalMovement(0.f),
      mDynamicCameraEnabled(Settings::Manager::getBool("dynamic camera", "Camera")),
      mDynamicCameraStateInitialized(false),
      mDynamicCameraWasOnGround(false),
      mDynamicCameraPreviousYaw(0.f),
      mDynamicCameraPreviousVerticalSpeed(0.f),
      mDynamicCameraPitch(0.f),
      mDynamicCameraPitchImpulse(0.f),
      mDynamicCameraRoll(0.f),
      mDynamicCameraStrafeRollStrength(Settings::Manager::getFloat("dynamic camera strafe roll", "Camera")),
      mDynamicCameraLookRollStrength(Settings::Manager::getFloat("dynamic camera look roll", "Camera")),
      mDynamicCameraJumpPitchStrength(Settings::Manager::getFloat("dynamic camera jump pitch", "Camera")),
      mDynamicCameraLandingPitchStrength(Settings::Manager::getFloat("dynamic camera landing pitch", "Camera")),
      mDynamicCameraSmoothing(Settings::Manager::getFloat("dynamic camera smoothing", "Camera")),
      mDeferredRotation(osg::Vec3f()),
      mDeferredRotationDisabled(false)
    {
        mCameraDistance = mBaseCameraDistance;

        mUpdateCallback = new UpdateRenderCameraCallback(this);
        mCamera->addUpdateCallback(mUpdateCallback);
    }

    Camera::~Camera()
    {
        mCamera->removeUpdateCallback(mUpdateCallback);
    }

    osg::Vec3d Camera::getFocalPointForView(bool firstPerson) const
    {
        if (!mAnimation)
            return osg::Vec3d();

        const osg::Node* trackingNode = nullptr;
        float heightScale = 1.f;
        if (firstPerson)
        {
            trackingNode = mAnimation->getNode("Camera");
            if (!trackingNode)
                trackingNode = mAnimation->getNode("Head");
        }
        else
        {
            SceneUtil::PositionAttitudeTransform* transform = mTrackingPtr.isEmpty()
                ? nullptr : mTrackingPtr.getRefData().getBaseNode();
            trackingNode = transform;
            if (transform)
                heightScale = transform->getScale().z();
        }

        if (!trackingNode)
            trackingNode = mTrackingNode.get();
        if (!trackingNode)
            return osg::Vec3d();

        const osg::NodePathList nodepaths = trackingNode->getParentalNodePaths();
        if (nodepaths.empty())
            return osg::Vec3d();

        const osg::Matrix worldMat = osg::computeLocalToWorld(nodepaths[0]);
        osg::Vec3d position = worldMat.getTrans();
        if (firstPerson)
        {
            // Head bobbing belongs to the actual first-person mesh. Do not apply
            // it while the normal third-person body is still visible during the
            // beginning of a view transition.
            if (isFirstPerson())
                position.z() += mHeadBobbingOffset;

            // With the immersive body enabled the stock Camera/Head node sits a little too
            // far back inside the upper torso on several skeletons. Move the eye slightly
            // forward on the horizontal facing axis only: pitching down must not drive the
            // camera into the chest.
            if (mImmersiveFirstPersonEnabled && mImmersiveFirstPersonForwardOffset != 0.f)
            {
                const osg::Quat yawOrientation(mYaw, osg::Vec3d(0.0, 0.0, 1.0));
                position += yawOrientation
                    * osg::Vec3d(0.0, mImmersiveFirstPersonForwardOffset, 0.0);
            }
        }
        else
        {
            position.z() += mHeight * heightScale;
            position.z() -= 10.f;
            position += getFocalPointOffset() + mFocalPointAdjustment;
        }
        return position;
    }

    osg::Vec3d Camera::getFocalPoint() const
    {
        return getFocalPointForView(isFirstPerson());
    }

    osg::Vec3d Camera::getFocalPointOffset() const
    {
        osg::Vec3d offset(0, 0, 10.f);
        offset.x() += mFocalPointCurrentOffset.x() * cos(getYaw());
        offset.y() += mFocalPointCurrentOffset.x() * sin(getYaw());
        offset.z() += mFocalPointCurrentOffset.y();
        return offset;
    }

    void Camera::getPositionForView(bool firstPerson, osg::Vec3d& focal, osg::Vec3d& camera) const
    {
        focal = getFocalPointForView(firstPerson);
        osg::Vec3d offset(0, 0, 0);
        if (!firstPerson)
        {
            const osg::Quat orient = osg::Quat(getPitch(), osg::Vec3d(1, 0, 0))
                * osg::Quat(getYaw(), osg::Vec3d(0, 0, 1));
            offset = orient * osg::Vec3d(0.f, -mCameraDistance, 0.f);
        }
        camera = focal + offset;
    }

    void Camera::getPosition(osg::Vec3d &focal, osg::Vec3d &camera) const
    {
        getPositionForView(isFirstPerson(), focal, camera);
    }

    void Camera::getCurrentViewPose(osg::Vec3d& position, osg::Vec3d& lookAt, osg::Vec3d& up) const
    {
        osg::Vec3d focal;
        getPositionForView(isFirstPerson(), focal, position);

        const float pitch = osg::clampBetween(mPitch + mDynamicCameraPitch,
            -static_cast<float>(osg::PI_2) + 0.000001f,
            static_cast<float>(osg::PI_2) - 0.000001f);
        const osg::Quat orientation = osg::Quat(mRoll, osg::Vec3d(0, 1, 0))
            * osg::Quat(pitch, osg::Vec3d(1, 0, 0))
            * osg::Quat(mYaw, osg::Vec3d(0, 0, 1));
        const osg::Vec3d forward = orientation * osg::Vec3d(0, 1, 0);
        up = orientation * osg::Vec3d(0, 0, 1);
        lookAt = position + forward;

        if (!mViewTransitionActive || mViewTransitionDuration <= 0.f)
            return;

        osg::Vec3d targetFocal;
        osg::Vec3d targetPosition;
        getPositionForView(mViewTransitionTargetFirstPerson, targetFocal, targetPosition);
        const osg::Vec3d targetLookAt = targetPosition + forward;
        const float linear = osg::clampBetween(
            mViewTransitionElapsed / mViewTransitionDuration, 0.f, 1.f);
        const float blend = linear * linear * (3.f - 2.f * linear);
        position = mViewTransitionStartPosition * (1.f - blend) + targetPosition * blend;
        lookAt = mViewTransitionStartLookAt * (1.f - blend) + targetLookAt * blend;
        up = mViewTransitionStartUp * (1.f - blend) + up * blend;
        if (up.length2() < 0.000001)
            up.set(0.0, 0.0, 1.0);
        else
            up.normalize();
    }

    void Camera::retargetViewTransition(bool targetFirstPerson)
    {
        osg::Vec3d currentPosition;
        osg::Vec3d currentLookAt;
        osg::Vec3d currentUp;
        getCurrentViewPose(currentPosition, currentLookAt, currentUp);

        mViewTransitionTargetFirstPerson = targetFirstPerson;
        mViewTransitionViewModeApplied = (mFirstPersonView == targetFirstPerson);
        mViewTransitionStartPosition = currentPosition;
        mViewTransitionStartLookAt = currentLookAt;
        mViewTransitionStartUp = currentUp;
        mViewTransitionElapsed = 0.f;
        mViewTransitionActive = true;
    }

    void Camera::preserveViewTransitionPoseAfterZoom(
        const osg::Vec3d& position, const osg::Vec3d& lookAt, const osg::Vec3d& up)
    {
        if (!mViewTransitionActive || mViewTransitionDuration <= 0.f)
            return;

        const float linear = osg::clampBetween(
            mViewTransitionElapsed / mViewTransitionDuration, 0.f, 1.f);
        const float blend = linear * linear * (3.f - 2.f * linear);
        const float remaining = 1.f - blend;
        if (remaining <= 0.0001f)
            return;

        osg::Vec3d targetFocal;
        osg::Vec3d targetPosition;
        getPositionForView(mViewTransitionTargetFirstPerson, targetFocal, targetPosition);

        const float pitch = osg::clampBetween(mPitch + mDynamicCameraPitch,
            -static_cast<float>(osg::PI_2) + 0.000001f,
            static_cast<float>(osg::PI_2) - 0.000001f);
        const osg::Quat orientation = osg::Quat(mRoll, osg::Vec3d(0, 1, 0))
            * osg::Quat(pitch, osg::Vec3d(1, 0, 0))
            * osg::Quat(mYaw, osg::Vec3d(0, 0, 1));
        const osg::Vec3d targetLookAt = targetPosition + orientation * osg::Vec3d(0, 1, 0);
        const osg::Vec3d targetUp = orientation * osg::Vec3d(0, 0, 1);

        // Keep the exact visible pose while changing only the destination.
        // This avoids a one-frame jump and preserves the remaining transition
        // time, so repeated wheel ticks accumulate immediately.
        mViewTransitionStartPosition = (position - targetPosition * blend) / remaining;
        mViewTransitionStartLookAt = (lookAt - targetLookAt * blend) / remaining;
        mViewTransitionStartUp = (up - targetUp * blend) / remaining;
    }

    void Camera::updateCamera(osg::Camera *cam)
    {
        osg::Vec3d focal, position;
        getPosition(focal, position);

        const float pitch = osg::clampBetween(mPitch + mDynamicCameraPitch,
            -static_cast<float>(osg::PI_2) + 0.000001f,
            static_cast<float>(osg::PI_2) - 0.000001f);
        osg::Quat orient = osg::Quat(mRoll, osg::Vec3d(0, 1, 0))
            * osg::Quat(pitch, osg::Vec3d(1, 0, 0)) * osg::Quat(mYaw, osg::Vec3d(0, 0, 1));
        osg::Vec3d forward = orient * osg::Vec3d(0,1,0);
        osg::Vec3d up = orient * osg::Vec3d(0,0,1);
        const osg::Vec3d normalLookAt = position + forward;

        if (mDialogueCameraActive && !mDialogueTarget.isEmpty())
        {
            MWBase::World* world = MWBase::Environment::get().getWorld();
            const MWWorld::Ptr playerPtr = world->getPlayerPtr();
            if (!playerPtr.isEmpty())
            {
                osg::Vec3d target = world->getActorHeadTransform(mDialogueTarget).getTrans();
                osg::Vec3d player = world->getActorHeadTransform(playerPtr).getTrans();

                const osg::Vec3d targetBase = mDialogueTarget.getRefData().getPosition().asVec3();
                const osg::Vec3d playerBase = playerPtr.getRefData().getPosition().asVec3();
                if (target.z() < targetBase.z() + 20.0)
                    target.z() = targetBase.z() + (mDialogueTarget.getClass().isNpc() ? 112.0 : 80.0);
                if (player.z() < playerBase.z() + 20.0)
                    player.z() = playerBase.z() + 112.0;

                // Aim slightly below the head so more of the torso stays visible during dialogue.
                target.z() -= mDialogueTarget.getClass().isNpc() ? 18.0 : 12.0;

                osg::Vec3d direction = target - player;
                const double actorDistance = direction.length();
                if (actorDistance > 1.0)
                {
                    direction /= actorDistance;
                    const double desiredDistance = osg::clampBetween(actorDistance * 0.56, 68.0, 190.0);
                    const double cameraDistance = std::min(desiredDistance, std::max(24.0, actorDistance - 8.0));
                    osg::Vec3d desiredPosition = target - direction * cameraDistance;
                    // Keep the cinematic dialogue camera a little above the actor's eye line.
                    // This leaves more of the NPC's upper body visible above the dialogue panel
                    // without turning the shot into a steep top-down angle.
                    if (mDialogueTarget.getClass().isNpc())
                        desiredPosition.z() += 8.0;

                    // Ignore actors while checking the line to the cinematic camera, otherwise the NPC itself
                    // can be mistaken for a wall because the ray starts at the actor's head.
                    const osg::Vec3f castFrom(target.x(), target.y(), target.z());
                    const osg::Vec3f castTo(desiredPosition.x(), desiredPosition.y(), desiredPosition.z());
                    const int collisionMask = MWPhysics::CollisionType_World | MWPhysics::CollisionType_HeightMap
                        | MWPhysics::CollisionType_Door;
                    const MWPhysics::RayCastingResult hit = world->getRayCasting()->castRay(
                        castFrom, castTo, MWWorld::ConstPtr(), std::vector<MWWorld::Ptr>(), collisionMask);
                    if (hit.mHit)
                    {
                        desiredPosition.set(hit.mHitPos.x() + hit.mHitNormal.x() * 5.0,
                            hit.mHitPos.y() + hit.mHitNormal.y() * 5.0,
                            hit.mHitPos.z() + hit.mHitNormal.z() * 5.0);
                    }

                    if (!mDialogueViewInitialized)
                    {
                        mDialogueCurrentPosition = position;
                        mDialogueCurrentLookAt = normalLookAt;
                        mDialogueViewInitialized = true;
                    }

                    const double smooth = 0.10;
                    mDialogueCurrentPosition = mDialogueCurrentPosition * (1.0 - smooth) + desiredPosition * smooth;
                    mDialogueCurrentLookAt = mDialogueCurrentLookAt * (1.0 - smooth) + target * smooth;

                    cam->setViewMatrixAsLookAt(mDialogueCurrentPosition, mDialogueCurrentLookAt, osg::Vec3d(0.0, 0.0, 1.0));
                    return;
                }
            }
        }
        else if (mDialogueViewInitialized)
        {
            // Restore the engine-owned camera matrix immediately in both first
            // and third person. Interpolating back can retain a stale cinematic
            // look-at vector and leave the normal camera visually offset.
            mDialogueViewInitialized = false;
            cam->setViewMatrixAsLookAt(position, normalLookAt, up);
            return;
        }

        if (mViewTransitionActive && mViewTransitionDuration > 0.f)
        {
            osg::Vec3d targetFocal;
            osg::Vec3d targetPosition;
            getPositionForView(mViewTransitionTargetFirstPerson, targetFocal, targetPosition);
            const osg::Vec3d targetLookAt = targetPosition + forward;

            const float linear = osg::clampBetween(
                mViewTransitionElapsed / mViewTransitionDuration, 0.f, 1.f);
            const float blend = linear * linear * (3.f - 2.f * linear);
            position = mViewTransitionStartPosition * (1.f - blend) + targetPosition * blend;
            const osg::Vec3d lookAt = mViewTransitionStartLookAt * (1.f - blend) + targetLookAt * blend;
            osg::Vec3d blendedUp = mViewTransitionStartUp * (1.f - blend) + up * blend;
            if (blendedUp.length2() < 0.000001)
                blendedUp.set(0.0, 0.0, 1.0);
            else
                blendedUp.normalize();
            cam->setViewMatrixAsLookAt(position, lookAt, blendedUp);
        }
        else
            cam->setViewMatrixAsLookAt(position, normalLookAt, up);
    }

    void Camera::setDialogueTarget(const MWWorld::Ptr& target)
    {
        mDialogueTarget = target;
        mDialogueCameraActive = !target.isEmpty();
        if (mDialogueCameraActive && mViewTransitionActive)
        {
            if (!mViewTransitionViewModeApplied)
                applyViewTransitionMode();
            mViewTransitionActive = false;
        }
        if (mDialogueCameraActive)
            mDialogueViewInitialized = false;
    }

    void Camera::clearDialogueTarget()
    {
        mDialogueCameraActive = false;
        mDialogueTarget = MWWorld::Ptr();
        // Do not keep a cinematic interpolation state after the dialogue ends.
        mDialogueViewInitialized = false;
    }

    void Camera::updateHeadBobbing(float duration) {
        static const float doubleStepLength = Settings::Manager::getFloat("head bobbing step", "Camera") * 2;
        static const float stepHeight = Settings::Manager::getFloat("head bobbing height", "Camera");
        static const float maxRoll = osg::DegreesToRadians(Settings::Manager::getFloat("head bobbing roll", "Camera"));

        if (MWBase::Environment::get().getWorld()->isOnGround(mTrackingPtr))
            mHeadBobbingWeight = std::min(mHeadBobbingWeight + duration * 5, 1.f);
        else
            mHeadBobbingWeight = std::max(mHeadBobbingWeight - duration * 5, 0.f);

        float doubleStepState = mTotalMovement / doubleStepLength - std::floor(mTotalMovement / doubleStepLength); // from 0 to 1 during 2 steps
        float stepState = std::abs(doubleStepState * 4 - 2) - 1; // from -1 to 1 on even steps and from 1 to -1 on odd steps
        float effect = (1 - std::cos(stepState * osg::DegreesToRadians(30.f))) * 7.5f; // range from 0 to 1
        float coef = std::min(mSmoothedSpeed / 300.f, 1.f) * mHeadBobbingWeight;
        mHeadBobbingOffset = (0.5f - effect) * coef * stepHeight; // range from -stepHeight/2 to stepHeight/2
        mHeadBobbingRoll = osg::sign(stepState) * effect * coef * maxRoll; // range from -maxRoll to maxRoll
    }

    void Camera::resetDynamicCameraState()
    {
        mDynamicCameraStateInitialized = false;
        mDynamicCameraPreviousVerticalSpeed = 0.f;
        mDynamicCameraPitch = 0.f;
        mDynamicCameraPitchImpulse = 0.f;
        mDynamicCameraRoll = 0.f;
    }

    void Camera::reloadSettings()
    {
        const bool immersiveFirstPersonEnabled
            = Settings::Manager::getBool("immersive first person", "Camera");
        const bool immersiveFirstPersonChanged
            = immersiveFirstPersonEnabled != mImmersiveFirstPersonEnabled;
        mImmersiveFirstPersonEnabled = immersiveFirstPersonEnabled;
        mImmersiveFirstPersonForwardOffset
            = Settings::Manager::getFloat("immersive first person forward offset", "Camera");
        mHeadBobbingEnabled = Settings::Manager::getBool("head bobbing", "Camera");
        mDynamicCameraEnabled = Settings::Manager::getBool("dynamic camera", "Camera");
        mDynamicCameraStrafeRollStrength = Settings::Manager::getFloat("dynamic camera strafe roll", "Camera");
        mDynamicCameraLookRollStrength = Settings::Manager::getFloat("dynamic camera look roll", "Camera");
        mDynamicCameraJumpPitchStrength = Settings::Manager::getFloat("dynamic camera jump pitch", "Camera");
        mDynamicCameraLandingPitchStrength = Settings::Manager::getFloat("dynamic camera landing pitch", "Camera");
        mDynamicCameraSmoothing = Settings::Manager::getFloat("dynamic camera smoothing", "Camera");

        if (!mDynamicCameraEnabled)
            resetDynamicCameraState();

        // Rebuild only the local player's render skeleton. The logical camera
        // mode and all multiplayer state remain unchanged.
        if (immersiveFirstPersonChanged && mAnimation && isFirstPerson())
            processViewChange();
    }

    void Camera::updateDynamicCamera(float duration)
    {
        if (!mDynamicCameraEnabled || duration <= 0.f || mTrackingPtr.isEmpty()
            || mDialogueCameraActive || mMode != Mode::Normal)
        {
            const float blend = 1.f - std::exp(-duration * std::max(1.f, mDynamicCameraSmoothing));
            mDynamicCameraPitch += (0.f - mDynamicCameraPitch) * blend;
            mDynamicCameraRoll += (0.f - mDynamicCameraRoll) * blend;
            mDynamicCameraPitchImpulse = 0.f;
            mDynamicCameraStateInitialized = false;
            return;
        }

        const osg::Vec3d position = mTrackingPtr.getRefData().getPosition().asVec3();
        MWBase::World* world = MWBase::Environment::get().getWorld();
        const bool onGround = world->isOnGround(mTrackingPtr);

        if (!mDynamicCameraStateInitialized)
        {
            mDynamicCameraPreviousPosition = position;
            mDynamicCameraPreviousYaw = mYaw;
            mDynamicCameraPreviousVerticalSpeed = 0.f;
            mDynamicCameraWasOnGround = onGround;
            mDynamicCameraStateInitialized = true;
            return;
        }

        const osg::Vec3d delta = position - mDynamicCameraPreviousPosition;
        if (delta.length2() > 1000.f * 1000.f)
        {
            resetDynamicCameraState();
            return;
        }

        const osg::Vec3d velocity = delta / duration;
        const float yawRate = Misc::normalizeAngle(mYaw - mDynamicCameraPreviousYaw) / duration;
        const osg::Quat yawOrientation(mYaw, osg::Vec3d(0, 0, 1));
        const osg::Vec3d right = yawOrientation * osg::Vec3d(1, 0, 0);
        const float strafeSpeed = static_cast<float>(velocity * right);

        const float strafeRoll = osg::clampBetween(-strafeSpeed / 450.f, -1.f, 1.f)
            * osg::DegreesToRadians(mDynamicCameraStrafeRollStrength);
        const float lookRoll = osg::clampBetween(-yawRate / 4.f, -1.f, 1.f)
            * osg::DegreesToRadians(mDynamicCameraLookRollStrength);
        const float targetRoll = strafeRoll + lookRoll;

        if (mDynamicCameraWasOnGround && !onGround)
            mDynamicCameraPitchImpulse += osg::DegreesToRadians(mDynamicCameraJumpPitchStrength);
        else if (!mDynamicCameraWasOnGround && onGround)
        {
            const float fallSpeed = std::max(0.f, -mDynamicCameraPreviousVerticalSpeed);
            const float landingScale = osg::clampBetween(fallSpeed / 500.f, 0.65f, 2.2f);
            mDynamicCameraPitchImpulse += osg::DegreesToRadians(
                mDynamicCameraLandingPitchStrength * landingScale);
        }

        mDynamicCameraPitchImpulse *= std::exp(-duration * 4.5f);
        const float blend = 1.f - std::exp(-duration * std::max(1.f, mDynamicCameraSmoothing));
        mDynamicCameraPitch += (mDynamicCameraPitchImpulse - mDynamicCameraPitch) * blend;
        mDynamicCameraRoll += (targetRoll - mDynamicCameraRoll) * blend;

        mDynamicCameraPreviousPosition = position;
        mDynamicCameraPreviousYaw = mYaw;
        mDynamicCameraPreviousVerticalSpeed = static_cast<float>(velocity.z());
        mDynamicCameraWasOnGround = onGround;
    }

    void Camera::reset()
    {
        togglePreviewMode(false);
        toggleVanityMode(false);
        if (!mFirstPersonView)
            toggleViewMode();
    }

    void Camera::rotateCamera(float pitch, float yaw, bool adjust)
    {
        if (adjust)
        {
            pitch += getPitch();
            yaw += getYaw();
        }
        setYaw(yaw);
        setPitch(pitch);
    }

    void Camera::update(float duration, bool paused)
    {
        if (mAnimation->upperBodyReady())
        {
            // Now process the view changes we queued earlier
            if (mVanityToggleQueued)
            {
                toggleVanityMode(mVanityToggleQueuedValue);
                mVanityToggleQueued = false;
            }
            if (mViewModeToggleQueued)
            {
                togglePreviewMode(false);
                toggleViewMode();
                mViewModeToggleQueued = false;
            }
        }

        if (paused)
            return;

        if (mViewTransitionActive)
        {
            mViewTransitionElapsed += std::max(0.f, duration);
            const float progress = mViewTransitionDuration > 0.f
                ? osg::clampBetween(mViewTransitionElapsed / mViewTransitionDuration, 0.f, 1.f)
                : 1.f;

            // Keep the full third-person body for most of a transition into
            // first person, then swap to the arms only when the camera is close
            // to the head. In the opposite direction, restore the full body near
            // the beginning so first-person arms never float in front of a camera
            // that is already pulling away.
            const float meshSwitchPoint = mViewTransitionTargetFirstPerson ? 0.82f : 0.18f;
            if (!mViewTransitionViewModeApplied && progress >= meshSwitchPoint)
                applyViewTransitionMode();

            if (progress >= 1.f)
            {
                if (!mViewTransitionViewModeApplied)
                    applyViewTransitionMode();
                mViewTransitionActive = false;
            }
        }

        // only show the crosshair in game mode
        MWBase::WindowManager *wm = MWBase::Environment::get().getWindowManager();
        wm->showCrosshair(!wm->isGuiMode() && mMode != Mode::Preview && mMode != Mode::Vanity
                          && (mFirstPersonView || mShowCrosshairInThirdPersonMode));

        if(mMode == Mode::Vanity)
            rotateCamera(0.f, osg::DegreesToRadians(3.f * duration), true);

        if (isFirstPerson() && mHeadBobbingEnabled)
            updateHeadBobbing(duration);
        else
            mHeadBobbingRoll = mHeadBobbingOffset = 0;

        updateDynamicCamera(duration);
        mRoll = mHeadBobbingRoll + mDynamicCameraRoll;

        updateFocalPointOffset(duration);
        updatePosition();

        float speed = mTrackingPtr.getClass().getCurrentSpeed(mTrackingPtr);
        mTotalMovement += speed * duration;
        speed /= (1.f + speed / 500.f);
        float maxDelta = 300.f * duration;
        mSmoothedSpeed += osg::clampBetween(speed - mSmoothedSpeed, -maxDelta, maxDelta);

        mMaxNextCameraDistance = mCameraDistance + duration * (100.f + mBaseCameraDistance);
        updateStandingPreviewMode();
    }

    void Camera::updatePosition()
    {
        mFocalPointAdjustment = osg::Vec3d();
        const bool needsThirdPersonTarget = !isFirstPerson()
            || (mViewTransitionActive && !mViewTransitionTargetFirstPerson);
        if (!needsThirdPersonTarget)
            return;

        const float cameraObstacleLimit = 5.0f;
        const float focalObstacleLimit = 10.f;

        const auto* rayCasting = MWBase::Environment::get().getWorld()->getRayCasting();

        // Adjust the third-person target even while the visible camera is
        // still transitioning out of first person. Otherwise the first wheel
        // tick targets a zero-distance camera and the next ticks repeatedly
        // restart the transition.
        osg::Vec3d focal = getFocalPointForView(false);
        osg::Vec3d focalOffset = getFocalPointOffset();
        float offsetLen = focalOffset.length();
        if (offsetLen > 0)
        {
            MWPhysics::RayCastingResult result = rayCasting->castSphere(focal - focalOffset, focal, focalObstacleLimit);
            if (result.mHit)
            {
                double adjustmentCoef = -(result.mHitPos + result.mHitNormal * focalObstacleLimit - focal).length() / offsetLen;
                mFocalPointAdjustment = focalOffset * std::max(-1.0, adjustmentCoef);
            }
        }

        // Calculate camera distance.
        mCameraDistance = mBaseCameraDistance + getCameraDistanceCorrection();
        if (mDynamicCameraDistanceEnabled)
            mCameraDistance = std::min(mCameraDistance, mMaxNextCameraDistance);
        osg::Vec3d cameraPos;
        getPositionForView(false, focal, cameraPos);
        MWPhysics::RayCastingResult result = rayCasting->castSphere(focal, cameraPos, cameraObstacleLimit);
        if (result.mHit)
            mCameraDistance = (result.mHitPos + result.mHitNormal * cameraObstacleLimit - focal).length();
    }

    void Camera::updateStandingPreviewMode()
    {
        if (!mStandingPreviewAllowed)
            return;
        float speed = mTrackingPtr.getClass().getCurrentSpeed(mTrackingPtr);
        bool combat = mTrackingPtr.getClass().isActor() &&
                      mTrackingPtr.getClass().getCreatureStats(mTrackingPtr).getDrawState() != MWMechanics::DrawState_Nothing;
        bool standingStill = speed == 0 && !combat && !mFirstPersonView;
        if (!standingStill && mMode == Mode::StandingPreview)
        {
            mMode = Mode::Normal;
            calculateDeferredRotation();
        }
        else if (standingStill && mMode == Mode::Normal)
            mMode = Mode::StandingPreview;
    }

    void Camera::setFocalPointTargetOffset(osg::Vec2d v)
    {
        mFocalPointTargetOffset = v;
        mPreviousTransitionSpeed = mFocalPointTransitionSpeed;
        mPreviousTransitionInfluence = 1.0f;
    }

    void Camera::updateFocalPointOffset(float duration)
    {
        if (duration <= 0)
            return;

        if (mSkipFocalPointTransition)
        {
            mSkipFocalPointTransition = false;
            mPreviousExtraOffset = osg::Vec2d();
            mPreviousTransitionInfluence = 0.f;
            mFocalPointCurrentOffset = mFocalPointTargetOffset;
        }

        osg::Vec2d oldOffset = mFocalPointCurrentOffset;

        if (mPreviousTransitionInfluence > 0)
        {
            mFocalPointCurrentOffset -= mPreviousExtraOffset;
            mPreviousExtraOffset = mPreviousExtraOffset / mPreviousTransitionInfluence + mPreviousTransitionSpeed * duration;
            mPreviousTransitionInfluence =
                std::max(0.f, mPreviousTransitionInfluence - duration * mFocalPointTransitionSpeedCoef);
            mPreviousExtraOffset *= mPreviousTransitionInfluence;
            mFocalPointCurrentOffset += mPreviousExtraOffset;
        }

        osg::Vec2d delta = mFocalPointTargetOffset - mFocalPointCurrentOffset;
        if (delta.length2() > 0)
        {
            float coef = duration * (1.0 + 5.0 / delta.length()) *
                         mFocalPointTransitionSpeedCoef * (1.0f - mPreviousTransitionInfluence);
            mFocalPointCurrentOffset += delta * std::min(coef, 1.0f);
        }
        else
        {
            mPreviousExtraOffset = osg::Vec2d();
            mPreviousTransitionInfluence = 0.f;
        }

        mFocalPointTransitionSpeed = (mFocalPointCurrentOffset - oldOffset) / duration;
    }

    void Camera::toggleViewMode(bool force)
    {
        // Changing the view will stop all playing animations, so if we are playing
        // anything important, queue the view change for later
        if (!mAnimation->upperBodyReady() && !force)
        {
            mViewModeToggleQueued = true;
            return;
        }
        else
            mViewModeToggleQueued = false;

        osg::Vec3d oldFocal;
        osg::Vec3d oldPosition;
        getPosition(oldFocal, oldPosition);
        (void)oldFocal;
        const float oldPitch = osg::clampBetween(mPitch + mDynamicCameraPitch,
            -static_cast<float>(osg::PI_2) + 0.000001f,
            static_cast<float>(osg::PI_2) - 0.000001f);
        const osg::Quat oldOrientation = osg::Quat(mRoll, osg::Vec3d(0, 1, 0))
            * osg::Quat(oldPitch, osg::Vec3d(1, 0, 0))
            * osg::Quat(mYaw, osg::Vec3d(0, 0, 1));

        const bool targetFirstPerson = mViewTransitionActive
            ? !mViewTransitionTargetFirstPerson : !mFirstPersonView;

        if (!force)
        {
            if (mViewTransitionActive)
            {
                // Reverse an unfinished transition from the exact visible pose
                // instead of snapping back to the logical first/third-person
                // state before starting again.
                retargetViewTransition(targetFirstPerson);
            }
            else
            {
                instantTransition();
                mViewTransitionTargetFirstPerson = targetFirstPerson;
                mViewTransitionViewModeApplied = (mFirstPersonView == targetFirstPerson);
                mViewTransitionStartPosition = oldPosition;
                mViewTransitionStartLookAt = oldPosition + oldOrientation * osg::Vec3d(0, 1, 0);
                mViewTransitionStartUp = oldOrientation * osg::Vec3d(0, 0, 1);
                mViewTransitionElapsed = 0.f;
                mViewTransitionActive = true;
            }
        }
        else
        {
            instantTransition();
            mViewTransitionActive = false;
            mViewTransitionTargetFirstPerson = targetFirstPerson;
            mViewTransitionViewModeApplied = false;
            applyViewTransitionMode();
        }
    }

    void Camera::applyViewTransitionMode()
    {
        if (mViewTransitionViewModeApplied)
            return;

        mFirstPersonView = mViewTransitionTargetFirstPerson;
        mViewTransitionViewModeApplied = true;
        updateStandingPreviewMode();
        processViewChange();
    }

    void Camera::allowVanityMode(bool allow)
    {
        if (!allow && mMode == Mode::Vanity)
        {
            disableDeferredPreviewRotation();
            toggleVanityMode(false);
        }
        mVanityAllowed = allow;
    }

    bool Camera::toggleVanityMode(bool enable)
    {
        // Changing the view will stop all playing animations, so if we are playing
        // anything important, queue the view change for later
        if (mFirstPersonView && !mAnimation->upperBodyReady())
        {
            mVanityToggleQueued = true;
            mVanityToggleQueuedValue = enable;
            return false;
        }

        if (!mVanityAllowed && enable)
            return false;

        if ((mMode == Mode::Vanity) == enable)
            return true;
        mMode = enable ? Mode::Vanity : Mode::Normal;
        if (!mDeferredRotationAllowed)
            disableDeferredPreviewRotation();
        if (!enable)
            calculateDeferredRotation();

        processViewChange();
        return true;
    }

    void Camera::togglePreviewMode(bool enable)
    {
        if (mFirstPersonView && !mAnimation->upperBodyReady())
            return;

        if((mMode == Mode::Preview) == enable)
            return;

        mMode = enable ? Mode::Preview : Mode::Normal;
        if (mMode == Mode::Normal)
            updateStandingPreviewMode();
        else if (mFirstPersonView)
            instantTransition();
        if (mMode == Mode::Normal)
        {
            if (!mDeferredRotationAllowed)
                disableDeferredPreviewRotation();
            calculateDeferredRotation();
        }
        processViewChange();
    }

    void Camera::setSneakOffset(float offset)
    {
        mAnimation->setFirstPersonOffset(osg::Vec3f(0,0,-offset));
    }

    void Camera::setYaw(float angle)
    {
        mYaw = Misc::normalizeAngle(angle);
    }

    void Camera::setPitch(float angle)
    {
        const float epsilon = 0.000001f;
        float limit = static_cast<float>(osg::PI_2) - epsilon;
        mPitch = osg::clampBetween(angle, -limit, limit);
    }

    float Camera::getCameraDistance() const
    {
        if (isFirstPerson() && !(mViewTransitionActive && !mViewTransitionTargetFirstPerson))
            return 0.f;
        return mCameraDistance;
    }

    void Camera::adjustCameraDistance(float delta)
    {
        if (mViewTransitionActive && getMode() != Mode::Preview && getMode() != Mode::Vanity)
        {
            osg::Vec3d visiblePosition;
            osg::Vec3d visibleLookAt;
            osg::Vec3d visibleUp;
            getCurrentViewPose(visiblePosition, visibleLookAt, visibleUp);

            if (mViewTransitionTargetFirstPerson)
            {
                // Scrolling out reverses the in-progress move to first person
                // from the exact visible pose. Scrolling further in simply
                // keeps the existing first-person target.
                if (delta > 0.f)
                {
                    mBaseCameraDistance = osg::clampBetween(
                        std::max(mBaseCameraDistance, mNearest) + delta, mNearest, mFurthest);
                    mCameraDistance = mBaseCameraDistance + getCameraDistanceCorrection();
                    retargetViewTransition(false);
                }
            }
            else
            {
                // While moving to third person, wheel input changes the target
                // distance directly. It must not call toggleViewMode for every
                // wheel notch, which previously made the camera jump back and
                // required excessive scrolling.
                const float unclamped = std::max(mBaseCameraDistance, mNearest) + delta;
                if (delta < 0.f && unclamped <= mNearest)
                {
                    // Capture the currently visible third-person transition
                    // before changing its distance target, then reverse it.
                    retargetViewTransition(true);
                    mBaseCameraDistance = mNearest;
                    mCameraDistance = mNearest + getCameraDistanceCorrection();
                }
                else
                {
                    mBaseCameraDistance = osg::clampBetween(unclamped, mNearest, mFurthest);
                    mCameraDistance = mBaseCameraDistance + getCameraDistanceCorrection();
                    preserveViewTransitionPoseAfterZoom(visiblePosition, visibleLookAt, visibleUp);
                }
            }

            mIsNearest = mBaseCameraDistance <= mNearest;
            Settings::Manager::setFloat("third person camera distance", "Camera", mBaseCameraDistance);
            return;
        }

        if (!isFirstPerson())
        {
            if(isNearest() && delta < 0.f && getMode() != Mode::Preview && getMode() != Mode::Vanity)
                toggleViewMode();
            else
                mBaseCameraDistance = std::min(mCameraDistance - getCameraDistanceCorrection(), mBaseCameraDistance) + delta;
        }
        else if (delta > 0.f)
        {
            // Preserve the remembered third-person distance. The first wheel
            // notch starts one smooth transition; subsequent notches are
            // handled by the transition branch above.
            mBaseCameraDistance = osg::clampBetween(
                std::max(mBaseCameraDistance, mNearest) + delta, mNearest, mFurthest);
            mCameraDistance = mBaseCameraDistance + getCameraDistanceCorrection();
            toggleViewMode();
        }

        mIsNearest = mBaseCameraDistance <= mNearest;
        mBaseCameraDistance = osg::clampBetween(mBaseCameraDistance, mNearest, mFurthest);
        Settings::Manager::setFloat("third person camera distance", "Camera", mBaseCameraDistance);
    }

    float Camera::getCameraDistanceCorrection() const
    {
        if (!mDynamicCameraDistanceEnabled)
            return 0;

        float pitchCorrection = std::max(-getPitch(), 0.f) * 50.f;

        float smoothedSpeedSqr = mSmoothedSpeed * mSmoothedSpeed;
        float speedCorrection = smoothedSpeedSqr / (smoothedSpeedSqr + 300.f*300.f) * mZoomOutWhenMoveCoef;

        return pitchCorrection + speedCorrection;
    }

    void Camera::setAnimation(NpcAnimation *anim)
    {
        mAnimation = anim;
        processViewChange();
    }

    void Camera::processViewChange()
    {
        if(isFirstPerson())
        {
            mAnimation->setViewMode(mImmersiveFirstPersonEnabled
                ? NpcAnimation::VM_ImmersiveFirstPerson : NpcAnimation::VM_FirstPerson);
            mTrackingNode = mAnimation->getNode("Camera");
            if (!mTrackingNode)
                mTrackingNode = mAnimation->getNode("Head");
            mHeightScale = 1.f;
        }
        else
        {
            mAnimation->setViewMode(NpcAnimation::VM_Normal);
            SceneUtil::PositionAttitudeTransform* transform = mTrackingPtr.getRefData().getBaseNode();
            mTrackingNode = transform;
            if (transform)
                mHeightScale = transform->getScale().z();
            else
                mHeightScale = 1.f;
        }
        rotateCamera(getPitch(), getYaw(), false);
    }

    void Camera::applyDeferredPreviewRotationToPlayer(float dt)
    {
        if (isVanityOrPreviewModeEnabled() || mTrackingPtr.isEmpty())
            return;

        osg::Vec3f rot = mDeferredRotation;
        float delta = rot.normalize();
        delta = std::min(delta, (delta + 1.f) * 3 * dt);
        rot *= delta;
        mDeferredRotation -= rot;

        if (mDeferredRotationDisabled)
        {
            mDeferredRotationDisabled = delta > 0.0001;
            rotateCameraToTrackingPtr();
            return;
        }

        auto& movement = mTrackingPtr.getClass().getMovementSettings(mTrackingPtr);
        movement.mRotation[0] += rot.x();
        movement.mRotation[1] += rot.y();
        movement.mRotation[2] += rot.z();
        if (std::abs(mDeferredRotation.z()) > 0.0001)
        {
            float s = std::sin(mDeferredRotation.z());
            float c = std::cos(mDeferredRotation.z());
            float x = movement.mPosition[0];
            float y = movement.mPosition[1];
            movement.mPosition[0] = x *  c + y * s;
            movement.mPosition[1] = x * -s + y * c;
        }
    }

    void Camera::rotateCameraToTrackingPtr()
    {
        setPitch(-mTrackingPtr.getRefData().getPosition().rot[0] - mDeferredRotation.x());
        setYaw(-mTrackingPtr.getRefData().getPosition().rot[2] - mDeferredRotation.z());
    }

    void Camera::instantTransition()
    {
        mSkipFocalPointTransition = true;
        mDeferredRotationDisabled = false;
        mDeferredRotation = osg::Vec3f();
        rotateCameraToTrackingPtr();
    }

    void Camera::calculateDeferredRotation()
    {
        MWWorld::Ptr ptr = mTrackingPtr;
        if (isVanityOrPreviewModeEnabled() || ptr.isEmpty())
            return;
        if (mFirstPersonView)
        {
            instantTransition();
            return;
        }

        mDeferredRotation.x() = Misc::normalizeAngle(-ptr.getRefData().getPosition().rot[0] - mPitch);
        mDeferredRotation.z() = Misc::normalizeAngle(-ptr.getRefData().getPosition().rot[2] - mYaw);
    }

}
