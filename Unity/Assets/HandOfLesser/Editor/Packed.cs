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
    // Each packed OSC parameter contains the same joint from both hands. The upper four bits hold
    // the left hand and the lower four bits hold the right hand. Separate blend trees decode both
    // values back into the -1 to +1 range used by the rest of the animation controller.
    class Packed
    {
        private static readonly int STEP_COUNT = 16; // 4 bits worth
        private static readonly int PACKED_ANIMATION_COUNT_RIGHT = AnimationValues.TOTAL_JOINT_COUNT / 2; // neg/pos for each joint on one hand
        private static readonly int PACKED_ANIMATION_COUNT_LEFT = (AnimationValues.TOTAL_JOINT_COUNT / 2 ) * STEP_COUNT; // 16 steps for each joint on one hand

        private static readonly int PACKED_ANIMATION_COUNT = PACKED_ANIMATION_COUNT_RIGHT + PACKED_ANIMATION_COUNT_LEFT;
        private static readonly int PACKED_VALUE_COUNT = 256;    // Max an 8bit int can store

        private static BlendTree generateDecodeTree(
            BlendTree parent,
            HandSide side,
            FingerType finger,
            FingerBendType joint,
            PropertyType outputProperty)
        {
            // outputProperty determines whether this decoder writes the live value or one of the
            // two retained interlace samples. All three are decoded from the same packed input.
            BlendTree tree = new BlendTree();
            AssetDatabase.AddObjectToAsset(tree, parent);

            tree.name = HOL.Resources.getJointParameterName(side, finger, joint, outputProperty);
            tree.blendType = BlendTreeType.Simple1D;
            tree.useAutomaticThresholds = false;
            tree.blendParameter = HOL.Resources.getJointParameterName(
                null,
                finger,
                joint,
                PropertyType.OSC_Packed);
            tree.hideFlags = HideFlags.HideInHierarchy;

            if (side == HandSide.left)
            {
                // The left-hand value is the upper nibble, so it remains unchanged for each block
                // of 16 packed values: 0-15 is step 0, 16-31 is step 1, and so on. Adding the same
                // clip at both ends of each block prevents Unity from interpolating while the
                // right-hand nibble changes inside that block.

                for (int i = 0; i < STEP_COUNT; i++)
                {
                    // Start
                    {
                        string animationPath = HOL.Resources.getAnimationOutputPath(HOL.Resources.getPackedAnimationClipName(finger, joint, i, outputProperty));
                        AnimationClip animation = AssetDatabase.LoadAssetAtPath<AnimationClip>(animationPath);

                        float threshold = i*STEP_COUNT;
                        tree.AddChild(animation, threshold);
                    }

                    // End
                    {
                        string animationPath = HOL.Resources.getAnimationOutputPath(HOL.Resources.getPackedAnimationClipName(finger, joint, i, outputProperty));
                        AnimationClip animation = AssetDatabase.LoadAssetAtPath<AnimationClip>(animationPath);

                        float threshold = i * STEP_COUNT + ( STEP_COUNT - 1 );
                        tree.AddChild(animation, threshold);
                    }
                }
            }
            else
            {
                // The right-hand value is the lower nibble, so it ramps from -1 to +1 over every
                // block of 16 packed values. Repeating the same two endpoints for all 16 blocks
                // discards the upper nibble and extracts only the right-hand value.

                for (int i = 0; i < PACKED_VALUE_COUNT; i+=STEP_COUNT)
                {
                    // Negative
                    {
                        string animationPath = HOL.Resources.getAnimationOutputPath(HOL.Resources.getAnimationClipName(HandSide.right, finger, joint, outputProperty, AnimationClipPosition.negative));
                        AnimationClip animation = AssetDatabase.LoadAssetAtPath<AnimationClip>(animationPath);

                        float threshold = i;
                        tree.AddChild(animation, threshold);
                    }

                    // Positive
                    {
                        string animationPath = HOL.Resources.getAnimationOutputPath(HOL.Resources.getAnimationClipName(HandSide.right, finger, joint, outputProperty, AnimationClipPosition.positive));
                        AnimationClip animation = AssetDatabase.LoadAssetAtPath<AnimationClip>(animationPath);

                        float threshold = i + (STEP_COUNT - 1);
                        tree.AddChild(animation, threshold);
                    }
                }
            }

            return tree;
        }

        private static void addDirectChild(List<ChildMotion> childTrees, Motion motion)
        {
            // Every joint decoder must run at full weight at the same time. Unity only exposes the
            // Direct Blend parameter when children are assigned as ChildMotion values, so collect
            // them here and assign the completed array to the root tree below.
            childTrees.Add(new ChildMotion()
            {
                directBlendParameter = HOL.Resources.ALWAYS_1_PARAMETER,
                motion = motion,
                timeScale = 1,
            });
        }

        private static void addBufferedDecodeTree(
            BlendTree root,
            List<ChildMotion> childTrees,
            HandSide side,
            FingerType finger,
            FingerBendType joint,
            PropertyType bufferProperty,
            bool updateWhenBitIsSet)
        {
            // The update decoder writes the current packed sample into this buffer. The surrounding
            // latch switches between that decoder and a self-feedback tree based on the interlace
            // bit, allowing the other buffer to retain the preceding sample.
            BlendTree updateTree = generateDecodeTree(
                root,
                side,
                finger,
                joint,
                bufferProperty);
            BlendTree latchTree = InterlacedBuffer.generateLatchTree(
                root,
                updateTree,
                side,
                finger,
                joint,
                bufferProperty,
                updateWhenBitIsSet);

            addDirectChild(childTrees, latchTree);
        }

        public static void populatePackedLayer(AnimatorController controller, bool interlaced)
        {
            AnimatorControllerLayer layer = ControllerLayer.inputPacked.findLayer(controller);

            // Full local OSC bypasses this layer. The packed state contains one Direct Blend Tree
            // so all joint decoders and interlace buffers can be evaluated together.
            AnimatorState disabledState = layer.stateMachine.AddState("HOLPackedDisabled");
            disabledState.writeDefaultValues = true;

            AnimatorState rootState = layer.stateMachine.AddState("HOLPacked");
            rootState.writeDefaultValues = true; // Must be true or values are multiplied depending on umber of blendtrees in controller!?!?!
            layer.stateMachine.defaultState = rootState;

            // The root has one child per live joint value, plus two buffered children per joint
            // when interlacing is enabled. Each child writes a different Animator parameter.
            BlendTree rootBlendtree = new BlendTree();
            AssetDatabase.AddObjectToAsset(rootBlendtree, rootState);

            rootBlendtree.name = "HandRoot";
            rootBlendtree.blendType = BlendTreeType.Direct;
            rootBlendtree.useAutomaticThresholds = false;
            rootBlendtree.blendParameter = HOL.Resources.ALWAYS_1_PARAMETER;
            
            rootState.motion = rootBlendtree;

            int blendtreesProcessed = 0;
            int blendtreeCount = AnimationValues.TOTAL_JOINT_COUNT * (interlaced ? 3 : 1);
            ProgressDisplay.updateBlendtreeProgress(blendtreesProcessed, blendtreeCount);

            // Cannot add directly to parent tree, see generateSmoothingBlendtree()
            List<ChildMotion> childTrees = new List<ChildMotion>();

            foreach (FingerType finger in new FingerType().Values())
            {
                foreach (FingerBendType joint in new FingerBendType().Values())
                {
                    PropertyType liveOutput = interlaced
                        ? PropertyType.input_interlaced
                        : PropertyType.input;

                    foreach (HandSide side in new HandSide().Values())
                    {
                        // Keep the current decoded value separate from the retained buffers. The
                        // combine layer uses it directly when the two samples are too far apart to
                        // represent a deliberate half-step.
                        addDirectChild(
                            childTrees,
                            generateDecodeTree(rootBlendtree, side, finger, joint, liveOutput));
                        blendtreesProcessed++;

                        if (interlaced)
                        {
                            // Buffer one updates on bit 0 and buffer two updates on bit 1. At any
                            // point they therefore contain the latest two transmitted samples.
                            addBufferedDecodeTree(
                                rootBlendtree,
                                childTrees,
                                side,
                                finger,
                                joint,
                                PropertyType.input_interlaced_first,
                                false);
                            addBufferedDecodeTree(
                                rootBlendtree,
                                childTrees,
                                side,
                                finger,
                                joint,
                                PropertyType.input_interlaced_second,
                                true);
                            blendtreesProcessed += 2;
                        }
                    }

                    ProgressDisplay.updateBlendtreeProgress(blendtreesProcessed, blendtreeCount);
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
        }

        // 0 to 15
        private static float getValueAtStep(float step, float stepCount, float startVal, float endVal)
        {
            // basically a re-range
            // Steps 0-15, or 16 steps. Subtract 1 from count to make the math work.
            // Also works for 0-255 if stepCount is 256
            float ratio = step / (stepCount - 1);
            return (ratio * endVal) + ((1f - ratio) * startVal);
        }

        private static void generatePackedAnimationLeft(FingerType finger, FingerBendType joint, int leftStep, PropertyType outputProperty)
        {
            // Animations for steps 0-15 of the left hand only
            // drives input
            AnimationClip clip = new AnimationClip();
            ClipTools.setClipProperty(
                ref clip,
                    HOL.Resources.getJointParameterName(HandSide.left, finger, joint, outputProperty),
                    getValueAtStep(leftStep, STEP_COUNT, -1, 1)
                );

            // Note separate animation clip name getter
            ClipTools.saveClip(clip, HOL.Resources.getAnimationOutputPath(HOL.Resources.getPackedAnimationClipName(finger, joint, leftStep, outputProperty)));
        }

        public static int generatedPackedAnimationRight(FingerType finger, FingerBendType joint, AnimationClipPosition position, PropertyType outputProperty)
        {
            // Basically just a negative and positive animation for each joint, with a multiplier because unity is a buggy mess
            // Drives input
            AnimationClip clip = new AnimationClip();
            ClipTools.setClipProperty(
                ref clip,
                    HOL.Resources.getJointParameterName(HandSide.right, finger, joint, outputProperty),
                    AnimationValues.getValueForPose(position)
                );

            ClipTools.saveClip(clip, HOL.Resources.getAnimationOutputPath(HOL.Resources.getAnimationClipName(HandSide.right, finger, joint, outputProperty, position)));

            return 1;
        }

        public static void generateAnimations(bool interlaced)
        {
            HOL.Resources.createOutputDirectories();

            int animationProcessed = 0;

            PropertyType[] properties;
            if (interlaced)
            {
                // Animation clips bind to a specific output parameter. The live value and both
                // retained values therefore need their own clip sets even though they decode the
                // same packed OSC input.
                properties = new PropertyType[] { PropertyType.input_interlaced, PropertyType.input_interlaced_first, PropertyType.input_interlaced_second };
            }
            else
            {
                properties = new PropertyType[] { PropertyType.input };
            }

            int animationCount = PACKED_ANIMATION_COUNT * properties.Length;
            ProgressDisplay.updateAnimationProgress(animationProcessed, animationCount);

            foreach (FingerType finger in new FingerType().Values())
            {
                foreach (FingerBendType joint in new FingerBendType().Values())
                {
                    foreach(PropertyType property in properties)
                    {
                        // We use two different methods for unpacking the data into left/right values.
                        // See each generator method for details, and generateDecodeTree() for how they are used.
                        animationProcessed += generatedPackedAnimationRight(finger, joint, AnimationClipPosition.negative, property);
                        animationProcessed += generatedPackedAnimationRight(finger, joint, AnimationClipPosition.positive, property);

                        for (int i = 0; i < STEP_COUNT; i++)
                        {
                            generatePackedAnimationLeft(finger, joint, i, property);
                            animationProcessed++;
                        }
                    }

                    ProgressDisplay.updateAnimationProgress(animationProcessed, animationCount);
                }
            }
        }
    }


}
