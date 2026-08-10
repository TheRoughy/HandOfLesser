using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using UnityEditor;
using UnityEditor.Animations;
using UnityEngine;
using VRC.SDK3.Avatars.Components;

namespace HOL
{
    class SmoothingAdjustment
    {
        // Order is important!
        private static int[] FrameRates = { 180, 144, 120, 100, 90, 72, 60, 50, 30 };

        // I definitely didn't have chatgpt write this for me
        public static float CalculateAdjustedSmoothingFactor(float originalAlpha, float referenceFrameRate, float currentFrameRate)
        {
            // Convert to frametime
            referenceFrameRate = 1000.0f / referenceFrameRate;
            currentFrameRate = 1000.0f / currentFrameRate;

            float frameTimeRatio = referenceFrameRate / currentFrameRate;
            return 1.0f - (float)Math.Pow(1.0f - originalAlpha, frameTimeRatio);

        }

        public static void generateSmoothingRateAdjustmentAnimation(float smoothing60Fps, float smoothingMax60fps)
        {
            // The amount of smoothing we want depends on the framerate, since
            // we want things to move the same amount in a certain amount of time.
            // We accomplish this by measuring the framerate ( frametime ), 
            // and using that to drive an animation of pre-computed adjusted values.

            // Thresholds must be added in order smallest-to-largest or unity will break!
            Keyframe[] keyframes = new Keyframe[FrameRates.Length];
            for (int i = 0; i < FrameRates.Length; i++)
            {
                // Calculate the adjusted smoothing ratio for each frame time
                float adjustedAlpha = CalculateAdjustedSmoothingFactor(smoothing60Fps, 60, FrameRates[i]);
                float adjustedMax = CalculateAdjustedSmoothingFactor(smoothingMax60fps, 60, FrameRates[i]);

                AnimationClip clip = new AnimationClip();
                ClipTools.setClipProperty(ref clip, Resources.getParameterName(PropertyType.smoothing_adjusted), adjustedAlpha);
                ClipTools.saveClip(clip, HOL.Resources.getAnimationOutputPath(PropertyType.smoothing_adjusted, (int)FrameRates[i]));

                clip = new AnimationClip();
                ClipTools.setClipProperty(ref clip, Resources.getParameterName(PropertyType.smoothing_adjusted_max), adjustedMax);
                ClipTools.saveClip(clip, HOL.Resources.getAnimationOutputPath(PropertyType.smoothing_adjusted_max, (int)FrameRates[i]));
            }
        }

        private static void addParameters(AnimatorController controller, float smoothing, float smoothingMax)
        {
            controller.AddParameter(new AnimatorControllerParameter()
            {
                name = Resources.getParameterName(PropertyType.smoothing_input),
                type = AnimatorControllerParameterType.Float,
                defaultFloat = smoothing  // whatever we set the smoothing to. Can we even edit via code afterwards? 
            });

            controller.AddParameter(new AnimatorControllerParameter()
            {
                name = Resources.getParameterName(PropertyType.smoothing_adjusted),
                type = AnimatorControllerParameterType.Float,
                defaultFloat = smoothing
            });

            controller.AddParameter(new AnimatorControllerParameter()
            {
                name = Resources.getParameterName(PropertyType.smoothing_adjusted_max),
                type = AnimatorControllerParameterType.Float,
                defaultFloat = smoothingMax
            });
        }

 


        public static int generateSmoothingAdjustmentBlendTree(BlendTree parent, List<ChildMotion> childTrees, PropertyType adjustedProperty)
        {
            // We will drive this using the smoothened frametime
            BlendTree tree = new BlendTree();
            AssetDatabase.AddObjectToAsset(tree, parent);
            tree.blendType = BlendTreeType.Simple1D;
            tree.name = HOL.Resources.getParameterName(PropertyType.fps_smooth);
            tree.useAutomaticThresholds = false;    // Automatic probably would work fine
            tree.blendParameter = HOL.Resources.getParameterName(PropertyType.fps_smooth);
            tree.hideFlags = HideFlags.HideInHierarchy;

            // In order to see the DirectBlendParamter required for the parent Direct blendtree, we need to use a ChildMotion,.
            // However, you cannot add a ChildMotion to a blendtree, and modifying it after adding it has no effect.
            // For whatever reason, adding them to a list assigning that as an array directly to BlendTree.Children works.
            childTrees.Add(new ChildMotion()
            {
                directBlendParameter = HOL.Resources.ALWAYS_1_PARAMETER,
                motion = tree,
                timeScale = 1,
            });


            // We are basically mapping frametimes to pre-computed adjusted values here here
            for (int i = 0; i < FrameRates.Length; i++)
            {
                int framerate = FrameRates[i];
                float frametime = 1.0f / (float)framerate;

                AnimationClip anim = AssetDatabase.LoadAssetAtPath<AnimationClip>(
                    HOL.Resources.getAnimationOutputPath(adjustedProperty, framerate));

                tree.AddChild(anim, frametime);
            }

            return 1;
        }

        public static void populateFpsSmoothingLayer(AnimatorController controller, float smoothingAmount, float smoothingAmountMax)
        {
            AnimatorControllerLayer layer = ControllerLayer.smoothingAdjustment.findLayer(controller);

            generateSmoothingRateAdjustmentAnimation(smoothingAmount, smoothingAmountMax);
            addParameters(controller, smoothingAmount, smoothingAmountMax);

            // Only used by the packed/interlaced smoothing path.
            AnimatorState disabledState = layer.stateMachine.AddState("HOLSmoothingAdjustmentDisabled");
            disabledState.writeDefaultValues = true;

            // State within this controller. TODO: attach to stuff
            AnimatorState rootState = layer.stateMachine.AddState("HOLSmoothingAdjustment");
            rootState.writeDefaultValues = true; // Must be true or values are multiplied depending on umber of blendtrees in controller!?!?!
            layer.stateMachine.defaultState = rootState;

            // Blendtree at the root of our state
            BlendTree rootBlendtree = new BlendTree();
            AssetDatabase.AddObjectToAsset(rootBlendtree, rootState);

            rootBlendtree.name = "smoothingAdjustment";
            rootBlendtree.blendType = BlendTreeType.Direct;
            rootBlendtree.useAutomaticThresholds = false;
            rootBlendtree.blendParameter = HOL.Resources.ALWAYS_1_PARAMETER;

            rootState.motion = rootBlendtree;

            // Cannot add directly to parent tree, see generateSmoothingBlendtree()
            List<ChildMotion> childTrees = new List<ChildMotion>();

            generateSmoothingAdjustmentBlendTree(rootBlendtree, childTrees, PropertyType.smoothing_adjusted);
            generateSmoothingAdjustmentBlendTree(rootBlendtree, childTrees, PropertyType.smoothing_adjusted_max);

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
    }
}
