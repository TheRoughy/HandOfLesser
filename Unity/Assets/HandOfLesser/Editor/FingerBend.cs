using HOL;
using HOL.FingerAnimations;
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
    class FingerBend
    {
        public static readonly int HUMANOID_BLENDTREE_COUNT = AnimationValues.TOTAL_JOINT_COUNT; // Bend joints ( including splay ) * fingers * hands
        public static readonly int HUMANOID_ANIMATION_COUNT = HUMANOID_BLENDTREE_COUNT * 2; // Positive and negative animation for each


        public static int generateCurlBlendTree(BlendTree parent, List<ChildMotion> childTrees, HandSide side, FingerType finger, FingerBendType joint)
        {
            BlendTree tree = new BlendTree();
            AssetDatabase.AddObjectToAsset(tree, parent);

            tree.blendType = BlendTreeType.Simple1D;
            tree.name = HOL.Resources.getJointParameterName(side, finger, joint, PropertyType.avatarRig);
            tree.useAutomaticThresholds = false;    // Automatic probably would work fine
            tree.blendParameter = HOL.Resources.getJointParameterName(side, finger, joint, PropertyType.smooth); // Note that we're using the smoothed proxy here

            // In order to see the DirectBlendParamter required for the parent Direct blendtree, we need to use a ChildMotion,.
            // However, you cannot add a ChildMotion to a blendtree, and modifying it after adding it has no effect.
            // For whatever reason, adding them to a list assigning that as an array directly to BlendTree.Children works.
            childTrees.Add(new ChildMotion()
            {
                directBlendParameter = HOL.Resources.ALWAYS_1_PARAMETER,
                motion = tree,
                timeScale = 1,
            });

            AnimationClip negativeAnimation = HOL.Resources.loadAnimationClip(
                HOL.Resources.getAnimationOutputPath(HOL.Resources.getAnimationClipName(side, finger, joint, PropertyType.avatarRig, AnimationClipPosition.negative)));
            AnimationClip positiveAnimation = HOL.Resources.loadAnimationClip(
                HOL.Resources.getAnimationOutputPath(HOL.Resources.getAnimationClipName(side, finger, joint, PropertyType.avatarRig, AnimationClipPosition.positive)));
            
            tree.AddChild(positiveAnimation, -1);
            tree.AddChild(negativeAnimation, 1);

            return 1;
        }

        private static AnimationClip GetAnimationClip(HandSide side, FingerType finger, AnimationClipPosition curlPosition, AnimationClipPosition splayPosition)
        {
            return HOL.Resources.loadAnimationClip(
                HOL.Resources.getAnimationOutputPath(
                    HOL.Resources.getAnimationClipName(
                        side,
                        finger,
                        FingerBendType.first,
                        PropertyType.avatarRigCombined,
                        curlPosition,
                        splayPosition  
                        )));
        }

        public static int generateCurlSplayBlendTree(BlendTree parent, List<ChildMotion> childTrees, HandSide side, FingerType finger, bool useSkeletal)
        {
            // This blendtree blends between 4 animations that represent that joint fully open and closed ( curled ), both with and without splay.
            // X is curl, Y is spread
            // The tree should be generated such that splay represents the rolling of the knuckle, along the z axis, before curl is applied.

            BlendTree tree = new BlendTree();
            AssetDatabase.AddObjectToAsset(tree, parent);

            tree.blendType = BlendTreeType.SimpleDirectional2D;
            tree.name = HOL.Resources.getJointParameterName(side, finger, FingerBendType.first, PropertyType.avatarRigCombined);
            tree.useAutomaticThresholds = false;    // Automatic probably would work fine
            tree.blendParameter = HOL.Resources.getJointParameterName(side, finger, FingerBendType.first, PropertyType.smooth); // Note that we're using the smoothed proxy here
            tree.blendParameterY = HOL.Resources.getJointParameterName(side, finger, FingerBendType.splay, PropertyType.smooth); // also smooth

            // In order to see the DirectBlendParamter required for the parent Direct blendtree, we need to use a ChildMotion,.
            // However, you cannot add a ChildMotion to a blendtree, and modifying it after adding it has no effect.
            // For whatever reason, adding them to a list assigning that as an array directly to BlendTree.Children works.
            childTrees.Add(new ChildMotion()
            {
                directBlendParameter = HOL.Resources.ALWAYS_1_PARAMETER,
                motion = tree,
                timeScale = 1,
            });

            // This is where we remap our knuckle-roll to humanoid in-out splay
            AnimationClip curl_open_splay_in = GetAnimationClip(side, finger, AnimationClipPosition.positive, AnimationClipPosition.negative);  
            AnimationClip curl_closed_splay_in = GetAnimationClip(side, finger, AnimationClipPosition.negative, AnimationClipPosition.negative);  
            AnimationClip curl_open_splay_out = GetAnimationClip(side, finger, AnimationClipPosition.positive, AnimationClipPosition.positive);  
            AnimationClip curl_closed_splay_out = GetAnimationClip(side, finger, AnimationClipPosition.negative, AnimationClipPosition.positive);  

            if(useSkeletal)
            {
                tree.AddChild(curl_open_splay_in, new Vector2(-1, -1));
                tree.AddChild(curl_closed_splay_in, new Vector2(1, -1));
                tree.AddChild(curl_open_splay_out, new Vector2(-1, 1));
                tree.AddChild(curl_closed_splay_out, new Vector2(1, 1));
            }
            else
            {
                // Invert splay here
                tree.AddChild(curl_open_splay_in, new Vector2(-1, -1));
                tree.AddChild(curl_closed_splay_in, new Vector2(1, -1));
                tree.AddChild(curl_open_splay_out, new Vector2(-1, 1));
                tree.AddChild(curl_closed_splay_out, new Vector2(1, 1));
            }



            return 2; // For the sake of counting, treat as two, one for curl and one for splay
        }

        public static void populateFingerJointLayer(AnimatorController controller, bool useSkeletal)
        {
            AnimatorControllerLayer layer = ControllerLayer.bend.findLayer(controller);

            // This mostly works like your average joint state machine
            // Accept -1 to 1, blend between corresponding negative and positive animation.

            // State within this controller. TODO: attach to stuff
            AnimatorState rootState = layer.stateMachine.AddState("HandOfLesserHands");
            rootState.writeDefaultValues = true; // Must be true or values are multiplied depending on umber of blendtrees in controller!?!?!

            // Blendtree at the root of our state
            BlendTree rootBlendtree = new BlendTree();
            AssetDatabase.AddObjectToAsset(rootBlendtree, rootState);


            // You /must/ create blendtrees using this to attach them to the controller
            // or they will be saved properly
            rootBlendtree.name = "HandRoot";
            rootBlendtree.blendType = BlendTreeType.Direct;
            rootBlendtree.useAutomaticThresholds = false;
            rootBlendtree.blendParameter = HOL.Resources.ALWAYS_1_PARAMETER;

            rootState.motion = rootBlendtree;

            // Generate the blendtrees for our joints
            FingerType[] fingers = (FingerType[])Enum.GetValues(typeof(FingerType));
            FingerBendType[] joints = (FingerBendType[])Enum.GetValues(typeof(FingerBendType));
            HandSide[] sides = (HandSide[])Enum.GetValues(typeof(HandSide));

            int blendtreesProcessed = 0;        // Cannot add directly to parent tree, see generateSmoothingBlendtree()

            ProgressDisplay.updateBlendtreeProgress(blendtreesProcessed, HUMANOID_BLENDTREE_COUNT);

            List<ChildMotion> childTrees = new List<ChildMotion>();
            foreach (HandSide side in sides)
            {
                foreach (FingerType finger in fingers)
                {
                    foreach (FingerBendType joint in joints)
                    {
                        if (joint == FingerBendType.splay)
                        {
                            // Merged with first
                            continue;
                        }

                        if (joint == FingerBendType.first)
                        {
                            // Humanoid rig splay does not work like real fingers, and operators on "open" and "closed" states.
                            // In real life, fully splayed means your fingers will be far part when uncurled, and close together have uncurled.
                            // In other words, they have the same rotation for unity's idea of "open" and "closed" depending on whether they are curled or uncurled.
                            // The app is responsible for sending special values that make the best of this nonsense.
                            // Even doing this curl is still incorrect, since they rotate on the z axis /after/ performing the curl on the x axis, rather than before.
                            // tl;dr the rotation order is wrong and fingers rotate wrong. Fix your fucking game engine Unity.
                            // Late: Not a problem for skeletal tho
                            blendtreesProcessed += generateCurlSplayBlendTree(rootBlendtree, childTrees, side, finger, useSkeletal);

                        }
                        else
                        {
                            blendtreesProcessed += generateCurlBlendTree(rootBlendtree, childTrees, side, finger, joint);
                        }
                        
                        ProgressDisplay.updateBlendtreeProgress(blendtreesProcessed, HUMANOID_BLENDTREE_COUNT);
                    }
                }
            }

            // Cannot add directly to parent tree, see generateSmoothingBlendtree()
            // Have to be added like this in order to set directblendparameter
            rootBlendtree.children = childTrees.ToArray();

            AssetDatabase.SaveAssets();

            ProgressDisplay.clearProgress();
        }

        public static void generateAnimations(GameObject avatar, bool useSkeletal)
        {
            HOL.Resources.createOutputDirectories();

            int animationProcessed = 0;
            ProgressDisplay.updateAnimationProgress(animationProcessed, HUMANOID_ANIMATION_COUNT);

            FingerAnimationInterface animGenerator = useSkeletal
                ? new FingerAnimations.SkeletalFingerAnimations(avatar)
                : new FingerAnimations.HumanoidFingerAnimations();

            foreach (HandSide side in new HandSide().Values())
            {
                foreach (FingerType finger in new FingerType().Values())
                {
                    foreach (FingerBendType joint in new FingerBendType().Values())
                    {
                        if (joint == FingerBendType.splay)
                        {
                            // Merged into first joint, skip
                            continue;
                        }

                        if (joint == FingerBendType.first)
                        {
                            // Closed and open, fully ACTUALLY splayed ( finger rolled outwards along the Z axis in an open state )
                            animationProcessed += animGenerator.generateCombinedCurlSplayAnimation(side, finger, AnimationClipPosition.negative, AnimationClipPosition.positive);
                            animationProcessed += animGenerator.generateCombinedCurlSplayAnimation(side, finger, AnimationClipPosition.positive, AnimationClipPosition.negative);

                            // Closed and open, not splayed ( rolled inwards, if at all )
                            animationProcessed += animGenerator.generateCombinedCurlSplayAnimation(side, finger, AnimationClipPosition.negative, AnimationClipPosition.negative);
                            animationProcessed += animGenerator.generateCombinedCurlSplayAnimation(side, finger, AnimationClipPosition.positive, AnimationClipPosition.positive);
                        }
                        else // Joints that don't have splay
                        {
                            // These animatiosn drive the humanoid rig ( for now )
                            animationProcessed += animGenerator.generateBendAnimation(side, finger, joint, AnimationClipPosition.negative);
                            animationProcessed += animGenerator.generateBendAnimation(side, finger, joint, AnimationClipPosition.positive);
                        }

                        ProgressDisplay.updateAnimationProgress(animationProcessed, HUMANOID_ANIMATION_COUNT);
                    }
                }
            }

            // supposedly don't actually need these
            //AssetDatabase.SaveAssets();
            //AssetDatabase.Refresh();

            ProgressDisplay.clearProgress();
        }

    }
}
