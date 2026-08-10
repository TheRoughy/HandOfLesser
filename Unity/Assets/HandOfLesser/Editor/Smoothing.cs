using System.Collections.Generic;
using UnityEditor;
using UnityEditor.Animations;
using UnityEngine;

namespace HOL
{
    class Smoothing
    {
        public static readonly int SMOOTHING_BLENDTREE_COUNT = AnimationValues.TOTAL_JOINT_COUNT; // Bend joints ( including splay ) * fingers * hands
        public static readonly int SMOOTHING_ANIMATION_COUNT = SMOOTHING_BLENDTREE_COUNT * 2; // Positive and negative animation for each

        // target_difference is half of the actual movement between reconstructed targets.
        private const float HalfStepDifference = 1.0f / 30.0f;
        private const float FullStepDifference = 2.0f / 30.0f;
        private const float LargerStepDifference = 3.0f / 30.0f;

        private enum SmoothingMode
        {
            normal,
            fullStep,
            halfStep,
        }

        public static int generateSmoothingAnimation(HandSide side, FingerType finger, FingerBendType joint, AnimationClipPosition position)
        {
            // This just sets the proxy parameter to whatever position, and we blend between these to do the smoothing
            // Note that these should always go from -1 to 1, which will not necessarily be the case of the normal finger animations
            // Right now they all just use the AnimationClipPosition though
            AnimationClip clip = new AnimationClip();
            ClipTools.setClipProperty(
                ref clip,
                    HOL.Resources.getJointParameterName(side, finger, joint, PropertyType.smooth),
                    AnimationValues.getValueForPose(position) // See definition
                );

            // SHOULD_BE_ONE_BUT_ISNT is used because when you have this animation output 1, it outputs 40 instead.
            // I have no idea why, but account for the scaling ahead of time and it's fine. fucking unity.

            ClipTools.saveClip(clip, HOL.Resources.getAnimationOutputPath(HOL.Resources.getAnimationClipName(side, finger, joint, PropertyType.smooth, position)));

            return 1;
        }

        public static BlendTree generatSmoothingBlendtreeInner( BlendTree parent, HandSide side, FingerType finger, FingerBendType joint, PropertyType propertType)
        {
            // generats blendtrees 1 and 2 for generateSmoothingBlendtree()
            BlendTree tree = new BlendTree();

            AssetDatabase.AddObjectToAsset(tree, parent);
            tree.name = HOL.Resources.getJointParameterName(side, finger, joint, propertType);
            tree.blendType = BlendTreeType.Simple1D;
            tree.useAutomaticThresholds = false;    // Automatic probably would work fine
            tree.hideFlags = HideFlags.HideInHierarchy;


            // Blend either by original param or proxy param
            tree.blendParameter = HOL.Resources.getJointParameterName(side, finger, joint, propertType);

            // Property type is reused here, first .normal denoting the blendtree driven by the original value,
            // and .proxy the one driven by the proxy.
            // Both drive the animations setting the proxy
            AnimationClip negativeAnimation = AssetDatabase.LoadAssetAtPath<AnimationClip>(
                HOL.Resources.getAnimationOutputPath(HOL.Resources.getAnimationClipName(side, finger, joint, PropertyType.smooth, AnimationClipPosition.negative)));
            AnimationClip positiveAnimation = AssetDatabase.LoadAssetAtPath<AnimationClip>(
                HOL.Resources.getAnimationOutputPath(HOL.Resources.getAnimationClipName(side, finger, joint, PropertyType.smooth, AnimationClipPosition.positive)));

            tree.AddChild(negativeAnimation, -1);
            tree.AddChild(positiveAnimation, 1);

            return tree;
        }

        private static float getSmoothingModeValue(SmoothingMode mode)
        {
            switch (mode)
            {
                case SmoothingMode.fullStep: return 0.5f;
                case SmoothingMode.halfStep: return 1.0f;
                default: return 0.0f;
            }
        }

        private static string getSmoothingModeAnimationPath(
            HandSide side,
            FingerType finger,
            FingerBendType joint,
            SmoothingMode mode)
        {
            string clipName = HOL.Resources.getJointParameterName(
                side,
                finger,
                joint,
                PropertyType.smoothing_mode);
            return HOL.Resources.getAnimationOutputPath(
                (clipName + "_" + mode).Replace('/', '_'));
        }

        private static AnimationClip loadSmoothingModeAnimation(
            HandSide side,
            FingerType finger,
            FingerBendType joint,
            SmoothingMode mode)
        {
            return AssetDatabase.LoadAssetAtPath<AnimationClip>(
                getSmoothingModeAnimationPath(side, finger, joint, mode));
        }

        private static BlendTree generateSmoothingModeHoldTree(
            BlendTree parent,
            HandSide side,
            FingerType finger,
            FingerBendType joint)
        {
            BlendTree tree = new BlendTree();
            AssetDatabase.AddObjectToAsset(tree, parent);

            string modeParameter = HOL.Resources.getJointParameterName(
                side,
                finger,
                joint,
                PropertyType.smoothing_mode);
            tree.name = modeParameter + "_hold";
            tree.blendType = BlendTreeType.Simple1D;
            tree.useAutomaticThresholds = false;
            tree.blendParameter = modeParameter;
            tree.hideFlags = HideFlags.HideInHierarchy;

            // Feeding the current mode back into itself preserves the smoothing selected by the
            // last target movement while subsequent network packets contain the same target.
            tree.AddChild(
                loadSmoothingModeAnimation(
                    side,
                    finger,
                    joint,
                    SmoothingMode.normal),
                0);
            tree.AddChild(
                loadSmoothingModeAnimation(
                    side,
                    finger,
                    joint,
                    SmoothingMode.halfStep),
                1);

            return tree;
        }

        private static BlendTree generateSmoothingModeTree(
            BlendTree parent,
            HandSide side,
            FingerType finger,
            FingerBendType joint)
        {
            BlendTree tree = new BlendTree();
            AssetDatabase.AddObjectToAsset(tree, parent);
            tree.name = HOL.Resources.getJointParameterName(
                side,
                finger,
                joint,
                PropertyType.smoothing_mode);
            tree.blendType = BlendTreeType.Simple1D;
            tree.useAutomaticThresholds = false;
            tree.blendParameter = HOL.Resources.getJointParameterName(
                side,
                finger,
                joint,
                PropertyType.target_difference);
            tree.hideFlags = HideFlags.HideInHierarchy;

            AnimationClip normalMode = loadSmoothingModeAnimation(
                side,
                finger,
                joint,
                SmoothingMode.normal);
            AnimationClip fullStepMode = loadSmoothingModeAnimation(
                side,
                finger,
                joint,
                SmoothingMode.fullStep);
            AnimationClip halfStepMode = loadSmoothingModeAnimation(
                side,
                finger,
                joint,
                SmoothingMode.halfStep);

            // Exact half- and full-step movements select their respective smoothing times. No
            // movement retains the previous selection; movement of 1.5 steps or more uses normal.
            tree.AddChild(normalMode, -LargerStepDifference);
            tree.AddChild(fullStepMode, -FullStepDifference);
            tree.AddChild(halfStepMode, -HalfStepDifference);
            tree.AddChild(generateSmoothingModeHoldTree(tree, side, finger, joint), 0);
            tree.AddChild(halfStepMode, HalfStepDifference);
            tree.AddChild(fullStepMode, FullStepDifference);
            tree.AddChild(normalMode, LargerStepDifference);

            return tree;
        }

        public static void addSmoothingModeParameters(AnimatorController controller)
        {
            foreach (HandSide side in new HandSide().Values())
            {
                foreach (FingerType finger in new FingerType().Values())
                {
                    foreach (FingerBendType joint in new FingerBendType().Values())
                    {
                        controller.AddParameter(new AnimatorControllerParameter()
                        {
                            name = HOL.Resources.getJointParameterName(
                                side,
                                finger,
                                joint,
                                PropertyType.smoothing_mode),
                            type = AnimatorControllerParameterType.Float,
                            defaultFloat = 0,
                        });
                    }
                }
            }
        }

        public static void populateSmoothingModeLayer(AnimatorController controller)
        {
            AnimatorControllerLayer layer = ControllerLayer.smoothingMode.findLayer(controller);
            AnimatorState activeState = layer.stateMachine.AddState("HOLSmoothingMode");
            activeState.writeDefaultValues = true;
            layer.stateMachine.defaultState = activeState;

            BlendTree activeRoot = new BlendTree();
            AssetDatabase.AddObjectToAsset(activeRoot, activeState);
            activeRoot.name = "SmoothingMode";
            activeRoot.blendType = BlendTreeType.Direct;
            activeRoot.useAutomaticThresholds = false;
            activeRoot.blendParameter = HOL.Resources.ALWAYS_1_PARAMETER;
            activeState.motion = activeRoot;

            List<ChildMotion> activeChildren = new List<ChildMotion>();
            foreach (HandSide side in new HandSide().Values())
            {
                foreach (FingerType finger in new FingerType().Values())
                {
                    foreach (FingerBendType joint in new FingerBendType().Values())
                    {
                        activeChildren.Add(new ChildMotion()
                        {
                            directBlendParameter = HOL.Resources.ALWAYS_1_PARAMETER,
                            motion = generateSmoothingModeTree(activeRoot, side, finger, joint),
                            timeScale = 1,
                        });
                    }
                }
            }
            activeRoot.children = activeChildren.ToArray();

            // Local full input bypasses smoothing. Explicitly clear the persisted modes so
            // returning to network input starts from normal smoothing rather than stale state.
            AnimatorState resetState = layer.stateMachine.AddState("HOLSmoothingModeReset");
            resetState.writeDefaultValues = true;
            BlendTree resetRoot = new BlendTree();
            AssetDatabase.AddObjectToAsset(resetRoot, resetState);
            resetRoot.name = "SmoothingModeReset";
            resetRoot.blendType = BlendTreeType.Direct;
            resetRoot.useAutomaticThresholds = false;
            resetRoot.blendParameter = HOL.Resources.ALWAYS_1_PARAMETER;
            resetState.motion = resetRoot;

            List<ChildMotion> resetChildren = new List<ChildMotion>();
            foreach (HandSide side in new HandSide().Values())
            {
                foreach (FingerType finger in new FingerType().Values())
                {
                    foreach (FingerBendType joint in new FingerBendType().Values())
                    {
                        resetChildren.Add(new ChildMotion()
                        {
                            directBlendParameter = HOL.Resources.ALWAYS_1_PARAMETER,
                            motion = loadSmoothingModeAnimation(
                                side,
                                finger,
                                joint,
                                SmoothingMode.normal),
                            timeScale = 1,
                        });
                    }
                }
            }
            resetRoot.children = resetChildren.ToArray();

            AnimatorStateTransition transition = activeState.AddTransition(resetState);
            transition.hasExitTime = false;
            transition.hasFixedDuration = true;
            transition.duration = 0;
            transition.canTransitionToSelf = false;
            transition.AddCondition(
                AnimatorConditionMode.Equals,
                1,
                HOL.Resources.USE_FULL_PARAMETER);

            transition = resetState.AddTransition(activeState);
            transition.hasExitTime = false;
            transition.hasFixedDuration = true;
            transition.duration = 0;
            transition.canTransitionToSelf = false;
            transition.AddCondition(
                AnimatorConditionMode.Equals,
                0,
                HOL.Resources.USE_FULL_PARAMETER);

            AssetDatabase.SaveAssets();
        }

        private static BlendTree generateRetentionTree(
            BlendTree parent,
            HandSide side,
            FingerType finger,
            FingerBendType joint,
            PropertyType retentionProperty)
        {
            // Blend the current input with the previous smoothed output. The global smoothing
            // amount is adjusted for the current frame time so every joint has the same response.
            BlendTree tree = new BlendTree();
            AssetDatabase.AddObjectToAsset(tree, parent);
            tree.blendType = BlendTreeType.Simple1D;
            tree.name = HOL.Resources.getParameterName(retentionProperty);
            tree.useAutomaticThresholds = false;
            tree.blendParameter = HOL.Resources.getParameterName(retentionProperty);
            tree.hideFlags = HideFlags.HideInHierarchy;

            tree.AddChild(generatSmoothingBlendtreeInner(tree, side, finger, joint, PropertyType.input), 0);
            tree.AddChild(generatSmoothingBlendtreeInner(tree, side, finger, joint, PropertyType.smooth), 1);

            return tree;
        }

        public static int generateSmoothingBlendtree(
            BlendTree parent,
            List<ChildMotion> childTrees,
            HandSide side,
            FingerType finger,
            FingerBendType joint,
            bool useStepSmoothing)
        {
            BlendTree tree;
            if (useStepSmoothing)
            {
                tree = new BlendTree();
                AssetDatabase.AddObjectToAsset(tree, parent);
                tree.blendType = BlendTreeType.Simple1D;
                tree.name = HOL.Resources.getJointParameterName(
                    side,
                    finger,
                    joint,
                    PropertyType.smoothing_mode);
                tree.useAutomaticThresholds = false;
                tree.blendParameter = HOL.Resources.getJointParameterName(
                    side,
                    finger,
                    joint,
                    PropertyType.smoothing_mode);
                tree.hideFlags = HideFlags.HideInHierarchy;

                tree.AddChild(
                    generateRetentionTree(
                        tree,
                        side,
                        finger,
                        joint,
                        PropertyType.smoothing_adjusted),
                    0);
                tree.AddChild(
                    generateRetentionTree(
                        tree,
                        side,
                        finger,
                        joint,
                        PropertyType.smoothing_full_step_adjusted),
                    0.5f);
                tree.AddChild(
                    generateRetentionTree(
                        tree,
                        side,
                        finger,
                        joint,
                        PropertyType.smoothing_half_step_adjusted),
                    1);
            }
            else
            {
                tree = generateRetentionTree(
                    parent,
                    side,
                    finger,
                    joint,
                    PropertyType.smoothing_adjusted);
            }

            // In order to see the DirectBlendParamter required for the parent Direct blendtree, we need to use a ChildMotion,.
            // However, you cannot add a ChildMotion to a blendtree, and modifying it after adding it has no effect.
            // For whatever reason, adding them to a list assigning that as an array directly to BlendTree.Children works.
            childTrees.Add(new ChildMotion()
            {
                directBlendParameter = HOL.Resources.ALWAYS_1_PARAMETER,
                motion = tree,
                timeScale = 1,
            });

            return 1;
        }

        public static void populateSmoothingLayer(
            AnimatorController controller,
            bool useStepSmoothing)
        {
            AnimatorControllerLayer layer = ControllerLayer.smoothing.findLayer(controller);

            // When UseFull is active, HOL_directInput writes smooth directly instead.
            AnimatorState disabledState = layer.stateMachine.AddState("HOLSmoothingDisabled");
            disabledState.writeDefaultValues = true;

            // State within this controller. TODO: attach to stuff
            AnimatorState rootState = layer.stateMachine.AddState("HOLSmoothing");
            rootState.writeDefaultValues = true; // Must be true or values are multiplied depending on umber of blendtrees in controller!?!?!
            layer.stateMachine.defaultState = rootState;

            // Blendtree at the root of our state
            BlendTree rootBlendtree = new BlendTree();
            AssetDatabase.AddObjectToAsset(rootBlendtree, rootState);

            rootBlendtree.name = "HandRoot_smoothing";
            rootBlendtree.blendType = BlendTreeType.Direct;
            rootBlendtree.useAutomaticThresholds = false;
            rootBlendtree.blendParameter = HOL.Resources.ALWAYS_1_PARAMETER;

            rootState.motion = rootBlendtree;

            int blendtreesProcessed = 0;
            ProgressDisplay.updateBlendtreeProgress(blendtreesProcessed, SMOOTHING_BLENDTREE_COUNT);

            // Cannot add directly to parent tree, see generateSmoothingBlendtree()
            List<ChildMotion> childTrees = new List<ChildMotion>();
            foreach (HandSide side in new HandSide().Values())
            {
                foreach (FingerType finger in new FingerType().Values())
                {
                    foreach (FingerBendType joint in new FingerBendType().Values())
                    {
                        blendtreesProcessed += generateSmoothingBlendtree(
                            rootBlendtree,
                            childTrees,
                            side,
                            finger,
                            joint,
                            useStepSmoothing);
                        ProgressDisplay.updateBlendtreeProgress(blendtreesProcessed, SMOOTHING_BLENDTREE_COUNT);
                    }
                }
            }
            // Cannot add directly to parent tree, see generateSmoothingBlendtree()
            // Have to be added like this in order to set directblendparameter
            rootBlendtree.children = childTrees.ToArray();

            AnimatorStateTransition transition = rootState.AddTransition(disabledState);
            transition.hasExitTime = false;
            transition.hasFixedDuration = true;
            transition.duration = 0;
            transition.canTransitionToSelf = false;
            transition.AddCondition(AnimatorConditionMode.Equals, 1, HOL.Resources.USE_FULL_PARAMETER);

            transition = disabledState.AddTransition(rootState);
            transition.hasExitTime = false;
            transition.hasFixedDuration = true;
            transition.duration = 0;
            transition.canTransitionToSelf = false;
            transition.AddCondition(AnimatorConditionMode.Equals, 0, HOL.Resources.USE_FULL_PARAMETER);

            AssetDatabase.SaveAssets();

            ProgressDisplay.clearProgress();
        }

        public static void generateAnimations()
        {
            HOL.Resources.createOutputDirectories();

            int animationProcessed = 0;
            ProgressDisplay.updateAnimationProgress(animationProcessed, SMOOTHING_ANIMATION_COUNT);

            foreach (HandSide side in new HandSide().Values())
            {
                foreach (FingerType finger in new FingerType().Values())
                {
                    foreach (FingerBendType joint in new FingerBendType().Values())
                    {
                        // While these drive the proxy parameter used for smoothing that drives the above
                        animationProcessed += generateSmoothingAnimation(side, finger, joint, AnimationClipPosition.negative);
                        animationProcessed += generateSmoothingAnimation(side, finger, joint, AnimationClipPosition.positive);

                        ProgressDisplay.updateAnimationProgress(animationProcessed, SMOOTHING_ANIMATION_COUNT);
                    }
                }
            }

            // supposedly don't actually need these
            //AssetDatabase.SaveAssets();
            //AssetDatabase.Refresh();

            // clearProgress();
        }

        private static void generateSmoothingModeAnimation(
            HandSide side,
            FingerType finger,
            FingerBendType joint,
            SmoothingMode mode)
        {
            AnimationClip clip = new AnimationClip();
            ClipTools.setClipProperty(
                ref clip,
                HOL.Resources.getJointParameterName(
                    side,
                    finger,
                    joint,
                    PropertyType.smoothing_mode),
                getSmoothingModeValue(mode));
            ClipTools.saveClip(
                clip,
                getSmoothingModeAnimationPath(side, finger, joint, mode));
        }

        public static void generateSmoothingModeAnimations()
        {
            HOL.Resources.createOutputDirectories();

            foreach (HandSide side in new HandSide().Values())
            {
                foreach (FingerType finger in new FingerType().Values())
                {
                    foreach (FingerBendType joint in new FingerBendType().Values())
                    {
                        generateSmoothingModeAnimation(
                            side,
                            finger,
                            joint,
                            SmoothingMode.normal);
                        generateSmoothingModeAnimation(
                            side,
                            finger,
                            joint,
                            SmoothingMode.fullStep);
                        generateSmoothingModeAnimation(
                            side,
                            finger,
                            joint,
                            SmoothingMode.halfStep);
                    }
                }
            }
        }
    }

}
