using System.Collections.Generic;
using UnityEditor;
using UnityEditor.Animations;
using UnityEngine;

namespace HOL
{
    class FrameRateMeasure
    {
        // A long clock keeps enough precision for frame timing while making wraparound infrequent.
        private const float ClockDurationSeconds = 1024.0f;
        private const float MaximumFrameTimeSeconds = 1.0f;

        private const string ClockClipName = "fps_clock";
        private const string FpsZeroClipName = "fps_zero";
        private const string FpsFromClockClipName = "fps_from_clock";
        private const string FpsFromPreviousClockClipName = "fps_from_previous_clock";
        private const string PreviousClockZeroClipName = "fps_previous_zero";
        private const string PreviousClockClipName = "fps_previous_from_clock";
        private const string SmoothFpsZeroClipName = "fps_smooth_zero";
        private const string SmoothFpsClipName = "fps_smooth_frame_time";

        private static string clipPath(string clipName)
        {
            return HOL.Resources.getAnimationOutputPath(clipName);
        }

        private static AnimationClip loadClip(string clipName)
        {
            return HOL.Resources.loadAnimationClip(clipPath(clipName));
        }

        private static void generateConstantAnimation(string clipName, PropertyType outputProperty, float value)
        {
            AnimationClip clip = new AnimationClip();
            ClipTools.setClipProperty(ref clip, HOL.Resources.getParameterName(outputProperty), value);
            ClipTools.saveClip(clip, clipPath(clipName));
        }

        private static void generateClockAnimation()
        {
            AnimationClip clip = new AnimationClip();
            ClipTools.setClipProperty(
                ref clip,
                HOL.Resources.getParameterName(PropertyType.fps_time),
                0.0f,
                0.0f,
                ClockDurationSeconds,
                ClockDurationSeconds);

            // The clock only wraps about once every 17 minutes. The one invalid delta at the wrap is
            // clamped by the frame-time smoothing tree and normal measurement resumes next frame.
            AnimationClipSettings settings = AnimationUtility.GetAnimationClipSettings(clip);
            settings.loopTime = true;
            AnimationUtility.SetAnimationClipSettings(clip, settings);

            ClipTools.saveClip(clip, clipPath(ClockClipName));
        }

        private static void generateAnimations()
        {
            HOL.Resources.createOutputDirectories();
            generateClockAnimation();

            // These clips turn 1D blend trees into simple parameter-copy and subtraction operations.
            generateConstantAnimation(FpsZeroClipName, PropertyType.fps, 0.0f);
            generateConstantAnimation(FpsFromClockClipName, PropertyType.fps, ClockDurationSeconds);
            generateConstantAnimation(
                FpsFromPreviousClockClipName, PropertyType.fps, -ClockDurationSeconds);

            generateConstantAnimation(PreviousClockZeroClipName, PropertyType.fps_previous, 0.0f);
            generateConstantAnimation(
                PreviousClockClipName, PropertyType.fps_previous, ClockDurationSeconds);

            generateConstantAnimation(SmoothFpsZeroClipName, PropertyType.fps_smooth, 0.0f);
            generateConstantAnimation(
                SmoothFpsClipName, PropertyType.fps_smooth, MaximumFrameTimeSeconds);
        }

        private static BlendTree generateCopyTree(
            BlendTree parent,
            string name,
            PropertyType inputProperty,
            string zeroClipName,
            string valueClipName,
            float maximumInput)
        {
            BlendTree tree = new BlendTree();
            AssetDatabase.AddObjectToAsset(tree, parent);
            tree.name = name;
            tree.blendType = BlendTreeType.Simple1D;
            tree.useAutomaticThresholds = false;
            tree.blendParameter = HOL.Resources.getParameterName(inputProperty);
            tree.hideFlags = HideFlags.HideInHierarchy;

            tree.AddChild(loadClip(zeroClipName), 0.0f);
            tree.AddChild(loadClip(valueClipName), maximumInput);
            return tree;
        }

        private static void addDirectChild(List<ChildMotion> children, Motion motion)
        {
            children.Add(new ChildMotion()
            {
                directBlendParameter = HOL.Resources.ALWAYS_1_PARAMETER,
                motion = motion,
                timeScale = 1.0f,
            });
        }

        public static void populateFpsMeasureLayer(AnimatorController controller)
        {
            generateAnimations();

            AnimatorControllerLayer layer = ControllerLayer.fpsMeasure.findLayer(controller);
            AnimatorState clockState = layer.stateMachine.AddState("HOLFpsClock");
            clockState.writeDefaultValues = true;
            clockState.motion = loadClip(ClockClipName);
            layer.stateMachine.defaultState = clockState;
        }

        public static void addParameters(AnimatorController controller)
        {
            controller.AddParameter(new AnimatorControllerParameter()
            {
                name = HOL.Resources.FPS_SMOOTHING_PARAMETER,
                type = AnimatorControllerParameterType.Float,
                defaultFloat = 0.99f
            });

            controller.AddParameter(
                HOL.Resources.getParameterName(PropertyType.fps_time),
                AnimatorControllerParameterType.Float);
            controller.AddParameter(
                HOL.Resources.getParameterName(PropertyType.fps_previous),
                AnimatorControllerParameterType.Float);
            controller.AddParameter(
                HOL.Resources.getParameterName(PropertyType.fps),
                AnimatorControllerParameterType.Float);
            controller.AddParameter(
                HOL.Resources.getParameterName(PropertyType.fps_smooth),
                AnimatorControllerParameterType.Float);
        }

        private static BlendTree generateFrameTimeSmoothingTree(BlendTree parent)
        {
            // Blend the latest frame time with the previous smoothed value. Reading fps_smooth while
            // also writing it gives us persistent animator-parameter feedback without a state change.
            BlendTree tree = new BlendTree();
            AssetDatabase.AddObjectToAsset(tree, parent);
            tree.name = "fpsSmoothing";
            tree.blendType = BlendTreeType.Simple1D;
            tree.useAutomaticThresholds = false;
            tree.blendParameter = HOL.Resources.FPS_SMOOTHING_PARAMETER;
            tree.hideFlags = HideFlags.HideInHierarchy;

            tree.AddChild(
                generateCopyTree(
                    tree,
                    "currentFrameTime",
                    PropertyType.fps,
                    SmoothFpsZeroClipName,
                    SmoothFpsClipName,
                    MaximumFrameTimeSeconds),
                0.0f);
            tree.AddChild(
                generateCopyTree(
                    tree,
                    "previousSmoothedFrameTime",
                    PropertyType.fps_smooth,
                    SmoothFpsZeroClipName,
                    SmoothFpsClipName,
                    MaximumFrameTimeSeconds),
                1.0f);

            return tree;
        }

        public static void populateFpsSmoothingLayer(AnimatorController controller)
        {
            AnimatorControllerLayer layer = ControllerLayer.fpsSmoothing.findLayer(controller);
            AnimatorState rootState = layer.stateMachine.AddState("HOLFpsProcessing");
            rootState.writeDefaultValues = true;
            layer.stateMachine.defaultState = rootState;

            BlendTree rootBlendtree = new BlendTree();
            AssetDatabase.AddObjectToAsset(rootBlendtree, rootState);
            rootBlendtree.name = "fpsProcessing";
            rootBlendtree.blendType = BlendTreeType.Direct;
            rootBlendtree.useAutomaticThresholds = false;
            rootBlendtree.blendParameter = HOL.Resources.ALWAYS_1_PARAMETER;
            rootState.motion = rootBlendtree;

            List<ChildMotion> children = new List<ChildMotion>();

            // fps = current clock - previous clock. Both trees write fps and their values add together.
            addDirectChild(
                children,
                generateCopyTree(
                    rootBlendtree,
                    "currentClock",
                    PropertyType.fps_time,
                    FpsZeroClipName,
                    FpsFromClockClipName,
                    ClockDurationSeconds));
            addDirectChild(
                children,
                generateCopyTree(
                    rootBlendtree,
                    "subtractPreviousClock",
                    PropertyType.fps_previous,
                    FpsZeroClipName,
                    FpsFromPreviousClockClipName,
                    ClockDurationSeconds));

            // Smooth before storing the current clock. The tree therefore reads the prior frame's
            // clock value, then updates it for the next Animator evaluation.
            addDirectChild(children, generateFrameTimeSmoothingTree(rootBlendtree));
            addDirectChild(
                children,
                generateCopyTree(
                    rootBlendtree,
                    "storeCurrentClock",
                    PropertyType.fps_time,
                    PreviousClockZeroClipName,
                    PreviousClockClipName,
                    ClockDurationSeconds));

            rootBlendtree.children = children.ToArray();
            AssetDatabase.SaveAssets();
            ProgressDisplay.clearProgress();
        }
    }
}
