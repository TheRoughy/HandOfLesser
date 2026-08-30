using System.Collections.Generic;
using UnityEditor;
using UnityEditor.Animations;
using UnityEngine;

namespace HOL
{
    class InterlacedWeigh
    {
        public static readonly int WEIGH_BLENDTREE_COUNT = AnimationValues.TOTAL_JOINT_COUNT;
        public static readonly int WEIGH_ANIMATION_COUNT = WEIGH_BLENDTREE_COUNT * 4;

        public static int generatedInterlacedWeightAnimation(
            HandSide side,
            FingerType finger,
            FingerBendType joint,
            AnimationClipPosition position,
            PropertyType property)
        {
            AnimationClip clip = new AnimationClip();
            ClipTools.setClipProperty(
                ref clip,
                HOL.Resources.getJointParameterName(side, finger, joint, property),
                AnimationValues.getValueForPose(position));

            ClipTools.saveClip(
                clip,
                HOL.Resources.getAnimationOutputPath(
                    HOL.Resources.getAnimationClipName(side, finger, joint, property, position)));

            return 1;
        }

        // Maps one input from -1..1 onto an output parameter. Reversing the endpoint clips
        // negates the value, allowing two trees to calculate a difference when blended evenly.
        private static BlendTree generateValueTree(
            BlendTree parent,
            HandSide side,
            FingerType finger,
            FingerBendType joint,
            PropertyType drivingProperty,
            PropertyType outputProperty,
            bool invert)
        {
            BlendTree tree = new BlendTree();
            AssetDatabase.AddObjectToAsset(tree, parent);
            tree.name = HOL.Resources.getJointParameterName(side, finger, joint, drivingProperty);
            tree.blendType = BlendTreeType.Simple1D;
            tree.useAutomaticThresholds = false;
            tree.blendParameter = HOL.Resources.getJointParameterName(
                side,
                finger,
                joint,
                drivingProperty);
            tree.hideFlags = HideFlags.HideInHierarchy;

            AnimationClipPosition negativePosition = invert
                ? AnimationClipPosition.positive
                : AnimationClipPosition.negative;
            AnimationClipPosition positivePosition = invert
                ? AnimationClipPosition.negative
                : AnimationClipPosition.positive;

            tree.AddChild(
                loadAnimation(side, finger, joint, outputProperty, negativePosition),
                -1);
            tree.AddChild(
                loadAnimation(side, finger, joint, outputProperty, positivePosition),
                1);

            return tree;
        }

        private static AnimationClip loadAnimation(
            HandSide side,
            FingerType finger,
            FingerBendType joint,
            PropertyType property,
            AnimationClipPosition position)
        {
            return HOL.Resources.loadAnimationClip(
                HOL.Resources.getAnimationOutputPath(
                    HOL.Resources.getAnimationClipName(side, finger, joint, property, position)));
        }

        private static BlendTree generateDifferenceTree(
            BlendTree parent,
            HandSide side,
            FingerType finger,
            FingerBendType joint,
            PropertyType firstProperty,
            PropertyType secondProperty,
            PropertyType outputProperty)
        {
            BlendTree tree = new BlendTree();
            AssetDatabase.AddObjectToAsset(tree, parent);
            tree.name = HOL.Resources.getJointParameterName(side, finger, joint, outputProperty);
            tree.blendType = BlendTreeType.Simple1D;
            tree.useAutomaticThresholds = false;
            tree.blendParameter = HOL.Resources.ALWAYS_HALF_PARAMETER;
            tree.hideFlags = HideFlags.HideInHierarchy;

            // The even blend produces (first - second) / 2. This is sufficient for comparing
            // discrete packed steps and avoids needing an Animator parameter with a value of two.
            tree.AddChild(
                generateValueTree(
                    tree,
                    side,
                    finger,
                    joint,
                    firstProperty,
                    outputProperty,
                    false),
                0);
            tree.AddChild(
                generateValueTree(
                    tree,
                    side,
                    finger,
                    joint,
                    secondProperty,
                    outputProperty,
                    true),
                1);

            return tree;
        }

        private static int generateInterlacedWeightTree(
            BlendTree parent,
            List<ChildMotion> childTrees,
            HandSide side,
            FingerType finger,
            FingerBendType joint)
        {
            BlendTree tree = generateDifferenceTree(
                parent,
                side,
                finger,
                joint,
                PropertyType.input_interlaced_first,
                PropertyType.input_interlaced_second,
                PropertyType.interlaced_weight);
            addDirectChild(childTrees, tree);
            return 1;
        }

        private static int generateTargetDifferenceTree(
            BlendTree parent,
            List<ChildMotion> childTrees,
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
                PropertyType.target_difference);
            tree.blendType = BlendTreeType.Simple1D;
            tree.useAutomaticThresholds = false;
            tree.blendParameter = HOL.Resources.INTERLACE_BIT_OSC_PARAMETER_NAME;
            tree.hideFlags = HideFlags.HideInHierarchy;
            addDirectChild(childTrees, tree);

            // The current bit identifies the target buffer updated this frame. The other buffer
            // still contains the preceding reconstructed target.
            tree.AddChild(
                generateDifferenceTree(
                    tree,
                    side,
                    finger,
                    joint,
                    PropertyType.input,
                    PropertyType.input_target_second,
                    PropertyType.target_difference),
                0);
            tree.AddChild(
                generateDifferenceTree(
                    tree,
                    side,
                    finger,
                    joint,
                    PropertyType.input,
                    PropertyType.input_target_first,
                    PropertyType.target_difference),
                1);

            return 1;
        }

        private static void addDirectChild(List<ChildMotion> childTrees, BlendTree tree)
        {
            childTrees.Add(new ChildMotion()
            {
                directBlendParameter = HOL.Resources.ALWAYS_1_PARAMETER,
                motion = tree,
                timeScale = 1,
            });
        }

        public static void addParameters(AnimatorController controller)
        {
            foreach (HandSide side in new HandSide().Values())
            {
                foreach (FingerType finger in new FingerType().Values())
                {
                    foreach (FingerBendType joint in new FingerBendType().Values())
                    {
                        controller.AddParameter(
                            HOL.Resources.getJointParameterName(
                                side,
                                finger,
                                joint,
                                PropertyType.interlaced_weight),
                            AnimatorControllerParameterType.Float);
                        controller.AddParameter(
                            HOL.Resources.getJointParameterName(
                                side,
                                finger,
                                joint,
                                PropertyType.target_difference),
                            AnimatorControllerParameterType.Float);
                    }
                }
            }
        }

        public static void populateWeighLayer(AnimatorController controller)
        {
            populateDifferenceLayer(controller, ControllerLayer.interlaceWeigh, false);
        }

        public static void populateTargetDifferenceLayer(AnimatorController controller)
        {
            populateDifferenceLayer(controller, ControllerLayer.targetDifference, true);
        }

        private static void populateDifferenceLayer(
            AnimatorController controller,
            ControllerLayer layerType,
            bool compareTargets)
        {
            AnimatorControllerLayer layer = layerType.findLayer(controller);
            string stateName = compareTargets ? "HOLTargetDifference" : "HOLWeigh";

            AnimatorState disabledState = layer.stateMachine.AddState(stateName + "Disabled");
            disabledState.writeDefaultValues = true;

            AnimatorState rootState = layer.stateMachine.AddState(stateName);
            rootState.writeDefaultValues = true;
            layer.stateMachine.defaultState = rootState;

            BlendTree rootTree = new BlendTree();
            AssetDatabase.AddObjectToAsset(rootTree, rootState);
            rootTree.name = compareTargets ? "TargetDifference" : "Weigh";
            rootTree.blendType = BlendTreeType.Direct;
            rootTree.useAutomaticThresholds = false;
            rootTree.blendParameter = HOL.Resources.ALWAYS_1_PARAMETER;
            rootState.motion = rootTree;

            int blendtreesProcessed = 0;
            ProgressDisplay.updateBlendtreeProgress(blendtreesProcessed, WEIGH_BLENDTREE_COUNT);
            List<ChildMotion> childTrees = new List<ChildMotion>();
            foreach (HandSide side in new HandSide().Values())
            {
                foreach (FingerType finger in new FingerType().Values())
                {
                    foreach (FingerBendType joint in new FingerBendType().Values())
                    {
                        blendtreesProcessed += compareTargets
                            ? generateTargetDifferenceTree(rootTree, childTrees, side, finger, joint)
                            : generateInterlacedWeightTree(rootTree, childTrees, side, finger, joint);
                        ProgressDisplay.updateBlendtreeProgress(
                            blendtreesProcessed,
                            WEIGH_BLENDTREE_COUNT);
                    }
                }
            }
            rootTree.children = childTrees.ToArray();

            addUseFullTransitions(rootState, disabledState);
            AssetDatabase.SaveAssets();
            ProgressDisplay.clearProgress();
        }

        private static void addUseFullTransitions(
            AnimatorState rootState,
            AnimatorState disabledState)
        {
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
        }

        public static void generateAnimations()
        {
            HOL.Resources.createOutputDirectories();

            int animationProcessed = 0;
            ProgressDisplay.updateAnimationProgress(animationProcessed, WEIGH_ANIMATION_COUNT);
            foreach (HandSide side in new HandSide().Values())
            {
                foreach (FingerType finger in new FingerType().Values())
                {
                    foreach (FingerBendType joint in new FingerBendType().Values())
                    {
                        foreach (PropertyType property in new[]
                        {
                            PropertyType.interlaced_weight,
                            PropertyType.target_difference
                        })
                        {
                            animationProcessed += generatedInterlacedWeightAnimation(
                                side,
                                finger,
                                joint,
                                AnimationClipPosition.negative,
                                property);
                            animationProcessed += generatedInterlacedWeightAnimation(
                                side,
                                finger,
                                joint,
                                AnimationClipPosition.positive,
                                property);
                        }

                        ProgressDisplay.updateAnimationProgress(
                            animationProcessed,
                            WEIGH_ANIMATION_COUNT);
                    }
                }
            }
        }
    }
}
