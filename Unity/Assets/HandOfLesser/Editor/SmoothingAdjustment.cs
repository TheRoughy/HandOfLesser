using System;
using System.Collections.Generic;
using UnityEditor;
using UnityEditor.Animations;
using UnityEngine;

namespace HOL
{
    class SmoothingAdjustment
    {
        // Thresholds must be added to a 1D blend tree from smallest frame time to largest.
        private static readonly int[] FrameRates = { 180, 144, 120, 100, 90, 72, 60, 50, 30 };

        // Reaching 95% of the target is close enough to consider the movement settled.
        private const float RemainingErrorAtSettlingTime = 0.05f;

        public static float calculateRetentionFactor(float smoothingTimeMS, float frameRate)
        {
            if (smoothingTimeMS <= 0.0f)
            {
                return 0.0f;
            }

            float frameTimeMS = 1000.0f / frameRate;
            return (float)Math.Pow(
                RemainingErrorAtSettlingTime,
                frameTimeMS / smoothingTimeMS);
        }

        private static void generateSmoothingAdjustmentAnimations(float smoothingTimeMS)
        {
            // Each animation contains the old-output weight needed at one frame rate. Interpolating
            // between them keeps the filter's real-time response stable as avatar FPS changes.
            foreach (int frameRate in FrameRates)
            {
                float retention = calculateRetentionFactor(smoothingTimeMS, frameRate);

                AnimationClip clip = new AnimationClip();
                ClipTools.setClipProperty(
                    ref clip,
                    Resources.getParameterName(PropertyType.smoothing_adjusted),
                    retention);
                ClipTools.saveClip(
                    clip,
                    HOL.Resources.getAnimationOutputPath(
                        PropertyType.smoothing_adjusted,
                        frameRate));
            }
        }

        private static void addParameter(AnimatorController controller, float smoothingTimeMS)
        {
            controller.AddParameter(new AnimatorControllerParameter()
            {
                name = Resources.getParameterName(PropertyType.smoothing_adjusted),
                type = AnimatorControllerParameterType.Float,
                defaultFloat = calculateRetentionFactor(smoothingTimeMS, 60.0f)
            });
        }

        private static BlendTree generateSmoothingAdjustmentBlendTree(
            BlendTree parent,
            List<ChildMotion> childTrees)
        {
            BlendTree tree = new BlendTree();
            AssetDatabase.AddObjectToAsset(tree, parent);
            tree.blendType = BlendTreeType.Simple1D;
            tree.name = HOL.Resources.getParameterName(PropertyType.fps_smooth);
            tree.useAutomaticThresholds = false;
            tree.blendParameter = HOL.Resources.getParameterName(PropertyType.fps_smooth);
            tree.hideFlags = HideFlags.HideInHierarchy;

            childTrees.Add(new ChildMotion()
            {
                directBlendParameter = HOL.Resources.ALWAYS_1_PARAMETER,
                motion = tree,
                timeScale = 1,
            });

            foreach (int frameRate in FrameRates)
            {
                float frameTime = 1.0f / frameRate;
                AnimationClip animation = AssetDatabase.LoadAssetAtPath<AnimationClip>(
                    HOL.Resources.getAnimationOutputPath(
                        PropertyType.smoothing_adjusted,
                        frameRate));

                tree.AddChild(animation, frameTime);
            }

            return tree;
        }

        public static void populateSmoothingAdjustmentLayer(
            AnimatorController controller,
            float smoothingTimeMS)
        {
            AnimatorControllerLayer layer = ControllerLayer.smoothingAdjustment.findLayer(controller);

            generateSmoothingAdjustmentAnimations(smoothingTimeMS);
            addParameter(controller, smoothingTimeMS);

            // Full local OSC bypasses network smoothing, so this layer can idle in that mode.
            AnimatorState disabledState = layer.stateMachine.AddState("HOLSmoothingAdjustmentDisabled");
            disabledState.writeDefaultValues = true;

            AnimatorState rootState = layer.stateMachine.AddState("HOLSmoothingAdjustment");
            rootState.writeDefaultValues = true;
            layer.stateMachine.defaultState = rootState;

            BlendTree rootBlendtree = new BlendTree();
            AssetDatabase.AddObjectToAsset(rootBlendtree, rootState);
            rootBlendtree.name = "smoothingAdjustment";
            rootBlendtree.blendType = BlendTreeType.Direct;
            rootBlendtree.useAutomaticThresholds = false;
            rootBlendtree.blendParameter = HOL.Resources.ALWAYS_1_PARAMETER;
            rootState.motion = rootBlendtree;

            List<ChildMotion> childTrees = new List<ChildMotion>();
            generateSmoothingAdjustmentBlendTree(rootBlendtree, childTrees);
            rootBlendtree.children = childTrees.ToArray();

            AnimatorStateTransition transition = rootState.AddTransition(disabledState);
            transition.hasExitTime = false;
            transition.hasFixedDuration = true;
            transition.duration = 0;
            transition.canTransitionToSelf = false;
            transition.AddCondition(
                AnimatorConditionMode.Equals,
                1,
                HOL.Resources.USE_FULL_PARAMETER);

            transition = disabledState.AddTransition(rootState);
            transition.hasExitTime = false;
            transition.hasFixedDuration = true;
            transition.duration = 0;
            transition.canTransitionToSelf = false;
            transition.AddCondition(
                AnimatorConditionMode.Equals,
                0,
                HOL.Resources.USE_FULL_PARAMETER);

            AssetDatabase.SaveAssets();
            ProgressDisplay.clearProgress();
        }
    }
}
