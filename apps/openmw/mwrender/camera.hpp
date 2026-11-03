#ifndef GAME_MWRENDER_CAMERA_H
#define GAME_MWRENDER_CAMERA_H

#include <string>

#include <osg/ref_ptr>
#include <osg/Vec3>
#include <osg/Vec3d>

#include "../mwworld/ptr.hpp"

namespace osg
{
    class Camera;
    class NodeCallback;
    class Node;
}

namespace MWRender
{
    class NpcAnimation;

    /// \brief Camera control
    class Camera
    {
    public:
        enum class Mode { Normal, Vanity, Preview, StandingPreview };

    private:
        MWWorld::Ptr mTrackingPtr;
        MWWorld::Ptr mDialogueTarget;
        bool mDialogueCameraActive;
        bool mDialogueViewInitialized;
        osg::Vec3d mDialogueCurrentPosition;
        osg::Vec3d mDialogueCurrentLookAt;
        osg::ref_ptr<const osg::Node> mTrackingNode;
        float mHeightScale;

        osg::ref_ptr<osg::Camera> mCamera;

        NpcAnimation *mAnimation;

        bool mFirstPersonView;
        Mode mMode;
        bool mVanityAllowed;
        bool mStandingPreviewAllowed;
        bool mDeferredRotationAllowed;

        float mNearest;
        float mFurthest;
        bool mIsNearest;

        float mHeight, mBaseCameraDistance;
        float mPitch, mYaw, mRoll;

        bool mVanityToggleQueued;
        bool mVanityToggleQueuedValue;
        bool mViewModeToggleQueued;

        // Smooth first/third-person blend used by the TAB view switch.
        bool mViewTransitionActive;
        bool mViewTransitionTargetFirstPerson;
        bool mViewTransitionViewModeApplied;
        float mViewTransitionElapsed;
        float mViewTransitionDuration;
        osg::Vec3d mViewTransitionStartPosition;
        osg::Vec3d mViewTransitionStartLookAt;
        osg::Vec3d mViewTransitionStartUp;

        float mCameraDistance;
        float mMaxNextCameraDistance;

        osg::Vec3d mFocalPointAdjustment;
        osg::Vec2d mFocalPointCurrentOffset;
        osg::Vec2d mFocalPointTargetOffset;
        float mFocalPointTransitionSpeedCoef;
        bool mSkipFocalPointTransition;

        // This fields are used to make focal point transition smooth if previous transition was not finished.
        float mPreviousTransitionInfluence;
        osg::Vec2d mFocalPointTransitionSpeed;
        osg::Vec2d mPreviousTransitionSpeed;
        osg::Vec2d mPreviousExtraOffset;

        float mSmoothedSpeed;
        float mZoomOutWhenMoveCoef;
        bool mDynamicCameraDistanceEnabled;
        bool mShowCrosshairInThirdPersonMode;

        bool mHeadBobbingEnabled;
        bool mImmersiveFirstPersonEnabled;
        float mImmersiveFirstPersonForwardOffset;
        float mHeadBobbingOffset;
        float mHeadBobbingRoll;
        float mHeadBobbingWeight; // Value from 0 to 1 for smooth enabling/disabling.
        float mTotalMovement; // Needed for head bobbing.
        void updateHeadBobbing(float duration);

        // Native ArenaMP port of the stable camera-motion part of Dynamic Camera.
        // It deliberately does not depend on Lua or OMWFX shaders.
        bool mDynamicCameraEnabled;
        bool mDynamicCameraStateInitialized;
        bool mDynamicCameraWasOnGround;
        osg::Vec3d mDynamicCameraPreviousPosition;
        float mDynamicCameraPreviousYaw;
        float mDynamicCameraPreviousVerticalSpeed;
        float mDynamicCameraPitch;
        float mDynamicCameraPitchImpulse;
        float mDynamicCameraRoll;
        float mDynamicCameraStrafeRollStrength;
        float mDynamicCameraLookRollStrength;
        float mDynamicCameraJumpPitchStrength;
        float mDynamicCameraLandingPitchStrength;
        float mDynamicCameraSmoothing;
        void updateDynamicCamera(float duration);
        void resetDynamicCameraState();

        void updateFocalPointOffset(float duration);
        osg::Vec3d getFocalPointForView(bool firstPerson) const;
        void getPositionForView(bool firstPerson, osg::Vec3d& focal, osg::Vec3d& camera) const;
        void getCurrentViewPose(osg::Vec3d& position, osg::Vec3d& lookAt, osg::Vec3d& up) const;
        void retargetViewTransition(bool targetFirstPerson);
        void preserveViewTransitionPoseAfterZoom(const osg::Vec3d& position, const osg::Vec3d& lookAt, const osg::Vec3d& up);
        void applyViewTransitionMode();
        void updatePosition();
        float getCameraDistanceCorrection() const;

        osg::ref_ptr<osg::NodeCallback> mUpdateCallback;

        // Used to rotate player to the direction of view after exiting preview or vanity mode.
        osg::Vec3f mDeferredRotation;
        bool mDeferredRotationDisabled;
        void calculateDeferredRotation();
        void updateStandingPreviewMode();

    public:
        Camera(osg::Camera* camera);
        ~Camera();

        /// Attach camera to object
        void attachTo(const MWWorld::Ptr &ptr) { mTrackingPtr = ptr; }
        MWWorld::Ptr getTrackingPtr() const { return mTrackingPtr; }

        void setFocalPointTransitionSpeed(float v) { mFocalPointTransitionSpeedCoef = v; }
        void setFocalPointTargetOffset(osg::Vec2d v);
        void instantTransition();
        void setDialogueTarget(const MWWorld::Ptr& target);
        void clearDialogueTarget();
        bool isDialogueCameraActive() const { return mDialogueCameraActive; }
        void enableDynamicCameraDistance(bool v) { mDynamicCameraDistanceEnabled = v; }
        void enableCrosshairInThirdPersonMode(bool v) { mShowCrosshairInThirdPersonMode = v; }

        /// Reload camera options that are safe to apply while the game is running.
        void reloadSettings();

        /// Update the view matrix of \a cam
        void updateCamera(osg::Camera* cam);

        /// Reset to defaults
        void reset();

        /// Set where the camera is looking at. Uses Morrowind (euler) angles
        /// \param rot Rotation angles in radians
        void rotateCamera(float pitch, float yaw, bool adjust);
        void rotateCameraToTrackingPtr();

        float getYaw() const { return mYaw; }
        void setYaw(float angle);

        float getPitch() const { return mPitch; }
        void setPitch(float angle);

        /// @param Force view mode switch, even if currently not allowed by the animation.
        void toggleViewMode(bool force=false);

        bool toggleVanityMode(bool enable);
        void allowVanityMode(bool allow);

        /// @note this may be ignored if an important animation is currently playing
        void togglePreviewMode(bool enable);

        void applyDeferredPreviewRotationToPlayer(float dt);
        void disableDeferredPreviewRotation() { mDeferredRotationDisabled = true; }

        /// \brief Lowers the camera for sneak.
        void setSneakOffset(float offset);

        bool isFirstPerson() const { return mFirstPersonView && mMode == Mode::Normal; }

        void processViewChange();

        void update(float duration, bool paused=false);

        /// Adds distDelta to the camera distance. Switches 3rd/1st person view if distance is less than limit.
        void adjustCameraDistance(float distDelta);

        float getCameraDistance() const;

        void setAnimation(NpcAnimation *anim);

        osg::Vec3d getFocalPoint() const;
        osg::Vec3d getFocalPointOffset() const;

        /// Stores focal and camera world positions in passed arguments
        void getPosition(osg::Vec3d &focal, osg::Vec3d &camera) const;

        bool isVanityOrPreviewModeEnabled() const { return mMode != Mode::Normal; }
        Mode getMode() const { return mMode; }

        bool isNearest() const { return mIsNearest; }
    };
}

#endif
