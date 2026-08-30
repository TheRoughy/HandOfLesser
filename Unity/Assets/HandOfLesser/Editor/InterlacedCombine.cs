using HOL;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using UnityEditor;
using UnityEditor.Animations;
using UnityEngine;

namespace HOL
{
    class InterlacedCombine
    {
        public static readonly int COMBINE_BLENDTREE_COUNT = AnimationValues.TOTAL_JOINT_COUNT; // Bend joints ( including splay ) * fingers * hands
        public static readonly int COMBINE_ANIMATION_COUNT = COMBINE_BLENDTREE_COUNT* 2;

        // Just sets input, because we don't normally generate a 1-to-1 input animation.
        public static int generateInputAnimation(HandSide side, FingerType finger, FingerBendType joint, AnimationClipPosition position)
        {
            // This just sets the proxy parameter to whatever position, and we blend between these to do the smoothing
            // Note that these should always go from -1 to 1, which will not necessarily be the case of the normal finger animations
            // Right now they all just use the AnimationClipPosition though
            AnimationClip clip = new AnimationClip();
            ClipTools.setClipProperty(
                ref clip,
                    HOL.Resources.getJointParameterName(side, finger, joint, PropertyType.input),
                    AnimationValues.getValueForPose(position) // See definition
                );

            ClipTools.saveClip(clip, HOL.Resources.getAnimationOutputPath(HOL.Resources.getAnimationClipName(side, finger, joint, PropertyType.input, position)));

            return 1;
        }

        // Single tree that uses drivingProperty to blend between outputProperty negative and postiive
        public static BlendTree generateValueDriverBlendtree( BlendTree parent, HandSide side, FingerType finger, FingerBendType joint, PropertyType drivingProperty, PropertyType outputProperty)
        {
            BlendTree tree = new BlendTree();

            AssetDatabase.AddObjectToAsset(tree, parent);
            tree.name = HOL.Resources.getJointParameterName(side, finger, joint, outputProperty);
            tree.blendType = BlendTreeType.Simple1D;
            tree.useAutomaticThresholds = false;    // Automatic probably would work fine
            tree.hideFlags = HideFlags.HideInHierarchy;


            tree.blendParameter = HOL.Resources.getJointParameterName(side, finger, joint, drivingProperty);

            AnimationClip negativeAnimation = HOL.Resources.loadAnimationClip(
                HOL.Resources.getAnimationOutputPath(HOL.Resources.getAnimationClipName(side, finger, joint, outputProperty, AnimationClipPosition.negative)));
            AnimationClip positiveAnimation = HOL.Resources.loadAnimationClip(
                HOL.Resources.getAnimationOutputPath(HOL.Resources.getAnimationClipName(side, finger, joint, outputProperty, AnimationClipPosition.positive)));

            tree.AddChild(negativeAnimation, -1);
            tree.AddChild(positiveAnimation, 1);

            return tree;
        }

        // Literally just a copypasta of the above but takes param name instead of a property
        // Will do a 50/50 of the children, in this case interlaced first/second average into input.
        public static BlendTree generateCombiningBlendTree(BlendTree parent, HandSide side, FingerType finger, FingerBendType joint)
        {
            BlendTree tree = new BlendTree();

            AssetDatabase.AddObjectToAsset(tree, parent);
            tree.name = HOL.Resources.getJointParameterName(side, finger, joint, PropertyType.input_interlaced);
            tree.blendType = BlendTreeType.Simple1D;
            tree.useAutomaticThresholds = false;    // Automatic probably would work fine
            tree.hideFlags = HideFlags.HideInHierarchy;

            // Will blend 50/50 between childre
            tree.blendParameter = HOL.Resources.ALWAYS_HALF_PARAMETER;

            tree.AddChild(generateValueDriverBlendtree(parent, side, finger, joint, PropertyType.input_interlaced_first, PropertyType.input), 0);
            tree.AddChild(generateValueDriverBlendtree(parent, side, finger, joint, PropertyType.input_interlaced_second, PropertyType.input), 1);

            return tree;
        }


        public static int generateFlipFlopBlendTree(BlendTree parent, List<ChildMotion> childTrees, HandSide side, FingerType finger, FingerBendType joint, PropertyType targetProperty)
        {
            // 
            // We'll blend between 3 trees. The center one will be a 50/50 blend between first and second,
            // outputting to input. 
            // two identical trees on either side will be driven directly by interlaced_input, i.e. the latest value.
            // Each of our 16 steps is about 0.125 of the -1/1 range, so -0.150  0   +0.150 or so

  
            BlendTree tree = new BlendTree();
            AssetDatabase.AddObjectToAsset(tree, parent);
            tree.blendType = BlendTreeType.Simple1D;
            tree.name = HOL.Resources.getJointParameterName(side, finger, joint, PropertyType.interlaced_weight);
            tree.useAutomaticThresholds = false;    // Automatic probably would work fine
            tree.blendParameter = HOL.Resources.getJointParameterName(side, finger, joint, PropertyType.interlaced_weight);
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

            // If weight is near 0, use average, otherwise use latest value.
            tree.AddChild(generateValueDriverBlendtree(tree, side, finger, joint, PropertyType.input_interlaced, PropertyType.input), -0.1f);
            tree.AddChild(generateCombiningBlendTree(tree, side, finger, joint), -0.066667f);
            tree.AddChild(generateCombiningBlendTree(tree, side, finger, joint), 0.066667f);
            tree.AddChild(generateValueDriverBlendtree(tree, side, finger, joint, PropertyType.input_interlaced, PropertyType.input), 0.1f);

            return 1;
        }

        // Blend between average of first/second, and the latest value, so the greater the distance the more it will move towards the latest.
        public static void populateCombineLayer(AnimatorController controller)
        {
            AnimatorControllerLayer layer = ControllerLayer.interlateOutput.findLayer(controller);

            AnimatorState disabledState = layer.stateMachine.AddState("HOLCombineDisabled");
            disabledState.writeDefaultValues = true;

            // State within this controller. TODO: attach to stuff
            AnimatorState rootState = layer.stateMachine.AddState("HOLCombine");
            rootState.writeDefaultValues = true; // Must be true or values are multiplied depending on umber of blendtrees in controller!?!?!
            layer.stateMachine.defaultState = rootState;

            // Blendtree at the root of our state
            BlendTree rootBlendtree = new BlendTree();
            AssetDatabase.AddObjectToAsset(rootBlendtree, rootState);

            rootBlendtree.name = "Combine";
            rootBlendtree.blendType = BlendTreeType.Direct;
            rootBlendtree.useAutomaticThresholds = false;
            rootBlendtree.blendParameter = HOL.Resources.ALWAYS_1_PARAMETER;

            rootState.motion = rootBlendtree;

            int blendtreesProcessed = 0;
            ProgressDisplay.updateBlendtreeProgress(blendtreesProcessed, COMBINE_BLENDTREE_COUNT);

            // Cannot add directly to parent tree, see generateSmoothingBlendtree()
            List<ChildMotion> childTrees = new List<ChildMotion>();
            foreach (HandSide side in new HandSide().Values())
            {
                foreach (FingerType finger in new FingerType().Values())
                {
                    foreach (FingerBendType joint in new FingerBendType().Values())
                    {
                        blendtreesProcessed += generateFlipFlopBlendTree(rootBlendtree, childTrees, side, finger, joint, PropertyType.input_interlaced_first);
                        ProgressDisplay.updateBlendtreeProgress(blendtreesProcessed, COMBINE_BLENDTREE_COUNT);
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

        // We don't normally need animations to drive input, because we either set them directly ( full )
        // or use fancy animations to set them very oddly ( packed ). When using interlaced we do need them tho.
        public static void generateAnimations()
        {
            HOL.Resources.createOutputDirectories();

            int animationProcessed = 0;
            ProgressDisplay.updateAnimationProgress(animationProcessed, COMBINE_ANIMATION_COUNT);

            foreach (HandSide side in new HandSide().Values())
            {
                foreach (FingerType finger in new FingerType().Values())
                {
                    foreach (FingerBendType joint in new FingerBendType().Values())
                    {
                        animationProcessed += generateInputAnimation(side, finger, joint, AnimationClipPosition.negative);
                        animationProcessed += generateInputAnimation(side, finger, joint, AnimationClipPosition.positive);

                        ProgressDisplay.updateAnimationProgress(animationProcessed, COMBINE_ANIMATION_COUNT);
                    }
                }
            }
        }
    }

}
