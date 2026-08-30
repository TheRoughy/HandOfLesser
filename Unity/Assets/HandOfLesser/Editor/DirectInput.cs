using HOL;
using System;
using System.Collections.Generic;
using UnityEditor;
using UnityEditor.Animations;
using UnityEngine;

namespace HOL
{
    class DirectInput
    {
        private static BlendTree generateValueDriverBlendtree(
            BlendTree parent,
            HandSide side,
            FingerType finger,
            FingerBendType joint)
        {
            BlendTree tree = new BlendTree();

            AssetDatabase.AddObjectToAsset(tree, parent);
            tree.name = HOL.Resources.getJointParameterName(side, finger, joint, PropertyType.smooth);
            tree.blendType = BlendTreeType.Simple1D;
            tree.useAutomaticThresholds = false;
            tree.hideFlags = HideFlags.HideInHierarchy;

            // Read from the dedicated raw full parameter. This must not share a name with any
            // parameter the Animator also writes, or the two sources will fight each other.
            tree.blendParameter = HOL.Resources.getJointParameterName(side, finger, joint, PropertyType.OSC_Full);

            AnimationClip negativeAnimation = HOL.Resources.loadAnimationClip(
                HOL.Resources.getAnimationOutputPath(
                    HOL.Resources.getAnimationClipName(
                        side,
                        finger,
                        joint,
                        PropertyType.smooth,
                        AnimationClipPosition.negative)));
            AnimationClip positiveAnimation = HOL.Resources.loadAnimationClip(
                HOL.Resources.getAnimationOutputPath(
                    HOL.Resources.getAnimationClipName(
                        side,
                        finger,
                        joint,
                        PropertyType.smooth,
                        AnimationClipPosition.positive)));

            tree.AddChild(negativeAnimation, -1);
            tree.AddChild(positiveAnimation, 1);

            return tree;
        }

        public static void populateLayer(AnimatorController controller)
        {
            AnimatorControllerLayer layer = ControllerLayer.directInput.findLayer(controller);

            AnimatorState disabledState = layer.stateMachine.AddState("HOLDirectInputDisabled");
            disabledState.writeDefaultValues = true;

            AnimatorState rootState = layer.stateMachine.AddState("HOLDirectInput");
            rootState.writeDefaultValues = true;
            // Keep this layer inactive unless local full is explicitly enabled.
            layer.stateMachine.defaultState = disabledState;

            BlendTree rootBlendtree = new BlendTree();
            AssetDatabase.AddObjectToAsset(rootBlendtree, rootState);

            rootBlendtree.name = "DirectInput";
            rootBlendtree.blendType = BlendTreeType.Direct;
            rootBlendtree.useAutomaticThresholds = false;
            rootBlendtree.blendParameter = HOL.Resources.ALWAYS_1_PARAMETER;

            rootState.motion = rootBlendtree;

            // Mirror the existing direct-blendtree style and give each joint its own raw full
            // to smooth copy tree.
            List<ChildMotion> childTrees = new List<ChildMotion>();
            foreach (HandSide side in new HandSide().Values())
            {
                foreach (FingerType finger in new FingerType().Values())
                {
                    foreach (FingerBendType joint in new FingerBendType().Values())
                    {
                        childTrees.Add(new ChildMotion()
                        {
                            directBlendParameter = HOL.Resources.ALWAYS_1_PARAMETER,
                            motion = generateValueDriverBlendtree(rootBlendtree, side, finger, joint),
                            timeScale = 1,
                        });
                    }
                }
            }

            rootBlendtree.children = childTrees.ToArray();

            // When UseFull is enabled, smooth is written directly from OSC_Full and the normal
            // smoothing layers are disabled separately.
            AnimatorStateTransition transition = disabledState.AddTransition(rootState);
            transition.hasExitTime = false;
            transition.hasFixedDuration = true;
            transition.duration = 0;
            transition.canTransitionToSelf = false;
            transition.AddCondition(AnimatorConditionMode.Equals, 1, HOL.Resources.USE_FULL_PARAMETER);

            transition = rootState.AddTransition(disabledState);
            transition.hasExitTime = false;
            transition.hasFixedDuration = true;
            transition.duration = 0;
            transition.canTransitionToSelf = false;
            transition.AddCondition(AnimatorConditionMode.Equals, 0, HOL.Resources.USE_FULL_PARAMETER);

            AssetDatabase.SaveAssets();
        }
    }
}
