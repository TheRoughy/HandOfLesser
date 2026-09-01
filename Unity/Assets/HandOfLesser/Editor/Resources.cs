using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using UnityEditor;
using UnityEngine;

namespace HOL
{
    class Resources
    {
        private static readonly string HANDOFLESSER_ASSET_PATH = "Assets/HandOfLesser";
        private static readonly string HANDOFLESSER_PACKAGE_PATH
            = "Packages/com.nordskog.handoflesser.modular-avatar";
        private static readonly string GENERATED_PATH = "Assets/HandOfLesser/generated";

        public static readonly string NAMESPACE = "HOL";

        public static readonly string HAND_SIDE_OSC_PARAMETER_NAME = addNamespacePrefix( 
            PropertyType.OSC_Alternating.addPrefixNamespace("hand_side"));

        public static readonly string INTERLACE_BIT_OSC_PARAMETER_NAME = addNamespacePrefix(
            PropertyType.OSC_Packed.addPrefixNamespace("interlace_bit"));

        public static readonly string FPS_SMOOTHING_PARAMETER = addNamespacePrefix("fps_smoothing_amount");
        public static readonly string USE_FULL_PARAMETER = addNamespacePrefix("UseFull");

        public static readonly string ALWAYS_1_PARAMETER = addNamespacePrefix( "always_one");

        public static readonly string ALWAYS_HALF_PARAMETER = addNamespacePrefix("always_half");
        private static string addNamespacePrefix(string param)
        {
            return NAMESPACE + "/" + param;
        }

        // Packed animations need separate 0-15 animations for the left hand only
        public static string getPackedAnimationClipName(FingerType finger, FingerBendType joint, int leftStep, PropertyType outputProperty)
        {
            StringBuilder builder = new StringBuilder();

            builder.Append("packed_");
            builder.Append(outputProperty.propertyName());
            builder.Append("_");
            builder.Append(finger.propertyName());
            builder.Append("_");
            builder.Append(joint.propertyName());
            builder.Append("_");
            builder.Append(leftStep.ToString());

            return builder.ToString();
        }

        public static string getAnimationClipName(HandSide? side, FingerType finger, FingerBendType joint, PropertyType outputProperty, AnimationClipPosition primaryPosition = AnimationClipPosition.neutral)
        {
            return getAnimationClipName(side, finger, joint, outputProperty, primaryPosition, null);
        }

        public static string getAnimationClipName(HandSide? side, FingerType finger, FingerBendType joint, PropertyType outputProperty, AnimationClipPosition primaryPosition, AnimationClipPosition? secondaryPosition )
        {
            string clipName = getJointParameterName(side, finger, joint, outputProperty);
            clipName = clipName + "_" + primaryPosition.propertyName();

            if (secondaryPosition.HasValue)
            {
                clipName = clipName + "_" + secondaryPosition.Value.propertyName();
            }

            // Probably don't want slashes in filenames
            return clipName.Replace('/', '_');
        }

        public static string getAnimationOutputPath(PropertyType outputProperty, int smoothingFramerate)
        {
            return getAnimationOutputPath(getAnimationClipName(outputProperty, smoothingFramerate));
        }

        public static string getAnimationOutputPath(PropertyType outputProperty)
        {
            return getAnimationOutputPath(getAnimationClipName(outputProperty));
        }

        public static string getAnimationOutputPath(PropertyType outputProperty, AnimationClipPosition position)
        {
            return getAnimationOutputPath(getAnimationClipName(outputProperty, position));
        }

        public static string getAnimationClipName(PropertyType outputProperty, int smoothingFramerate)
        {
            string clipName = outputProperty.propertyName() + "_" + smoothingFramerate;

            // Probably don't want slashes in filenames
            return clipName.Replace('/', '_');
        }

        public static string getAnimationClipName(PropertyType outputProperty)
        {
            string clipName = outputProperty.propertyName();

            // Probably don't want slashes in filenames
            return clipName.Replace('/', '_');
        }

        public static string getAnimationClipName(PropertyType outputProperty, AnimationClipPosition primaryPosition)
        {
            string clipName = outputProperty.propertyName();
            clipName = clipName + "_" + primaryPosition.propertyName();

            // Probably don't want slashes in filenames
            return clipName.Replace('/', '_');
        }

        // Only for use with fps and fpsSmooth
        public static string getParameterName(PropertyType outputProperty)
        {
            return addNamespacePrefix(outputProperty.propertyName());
        }

        public static string getJointParameterName( HandSide? side, FingerType finger, FingerBendType joint, PropertyType propertyType)
        {
            bool hasSide = true;

            switch(propertyType)
            {
                case PropertyType.OSC_Alternating:
                case PropertyType.OSC_Packed:
                case PropertyType.input_packed:
                    {
                    // the param may be supplied even when dealing with these.
                    // This is easier than checking the enum over and over.
                    hasSide = false;
                    break;
                }
                default:
                {
                    if (!side.HasValue)
                    {
                        throw new ArgumentException("Side may only be null if the PropertType is OSC Alternating or Packed!");
                    }
                    break;
                }
            }

            if (propertyType == PropertyType.avatarRig)
            {
                // For now driving humanoid rig, will use our own animations for this in the future
                return getHumanoidRigJointPropertyName(side.Value, finger, joint);
            }

            string name = getPlainJointParameterName(finger, joint);
            if (hasSide)
            {
                // Some param that includes the hand side
                name = side.Value.propertyName() + "_" + name;
            }

            name = propertyType.addPrefixNamespace(name);

            // All parameters should be lower case, except for the HOL/ prefix.
            // If you want to change this you will have to mess with the c++ side too.
            // Note that getHumanoidRigJointPropertyName() returns above without
            // converting to lowercase. This is because the humanoid rig paramenters 
            // need to be a specific case, and the enum propertyNames reflect this.
            name = name.ToLower();

            return addNamespacePrefix(name);
        }

        // Generic finger/joint parameter name with no qualifiers, including no left/right side
        private static string getPlainJointParameterName(FingerType finger, FingerBendType joint)
        {
            StringBuilder builder = new StringBuilder();

            builder.Append(finger.propertyName());

            if (joint == FingerBendType.splay)
            {
                builder.Append("_");
                builder.Append("splay");
            }
            else
            {
                builder.Append("_");
                builder.Append(joint.propertyName());
                builder.Append("_");
                builder.Append("curl");
            }

            return builder.ToString();
        }

        private static string getHumanoidRigJointPropertyName(HandSide? side, FingerType finger, FingerBendType joint)
        {
            // Right Hand.Thumb.1 Stretched
            StringBuilder builder = new StringBuilder();

            // We reuse this for blendtree names that cover both hands, so don't need side
            if (side.HasValue)
            {
                builder.Append(side.Value.propertyName());
                builder.Append(".");
            }

            builder.Append(finger.propertyName());

            if (joint == FingerBendType.splay)
            {
                builder.Append(".");
                builder.Append("Spread");
            }
            else
            {
                builder.Append(".");
                builder.Append(joint.propertyName());
                builder.Append(" ");
                builder.Append("Stretched");
            }


            return builder.ToString();
        }

        public static string getAnimationOutputPath(string animationClipName)
        {
            return Path.Combine("Assets", "HandOfLesser", "generated", "animations", animationClipName + ".anim");
        }

        public static string getAnimationControllerOutputPath()
        {
            return Path.Combine("Assets", "HandOfLesser", "generated", "handoflesser_controller" + ".controller");
        }

        public static string getFingerTrackingControllerOutputPath()
        {
            return Path.Combine(
                "Assets",
                "HandOfLesser",
                "generated",
                "handoflesser_finger_tracking.controller");
        }

        public static string getAnimationContainerPath()
        {
            return GENERATED_PATH + "/handoflesser_animations.asset";
        }

        public static AnimationClip loadAnimationClip(string path)
        {
            return ClipTools.loadClip(path);
        }

        public static void createOutputDirectories()
        {
            if (!AssetDatabase.IsValidFolder("Assets/HandOfLesser"))
            {
                AssetDatabase.CreateFolder("Assets", "HandOfLesser");
            }

            if (!AssetDatabase.IsValidFolder("Assets/HandOfLesser/generated"))
            {
                AssetDatabase.CreateFolder("Assets/HandOfLesser", "generated");
            }

        }

        public static AvatarMask getBothHandsMask()
        {
            return AssetDatabase.LoadAssetAtPath<AvatarMask>(getSourcePath() + "/vrc_handsonly.mask");
        }

        public static AvatarMask getBothHandsSkeletalMask()
        {
            return AssetDatabase.LoadAssetAtPath<AvatarMask>(getSourcePath() + "/vrc_handsonly_skeletal.mask");
        }

        public static string getParametersPath()
        {
            return GENERATED_PATH + "/handoflesser_parameters.asset";
        }

        public static string getGeneratedOutputPath()
        {
            return GENERATED_PATH;
        }

        public static string getModularAvatarPrefabPath()
        {
            return GENERATED_PATH + "/HandOfLesser_ModularAvatar.prefab";
        }

        private static string getSourcePath()
        {
            if (AssetDatabase.IsValidFolder(HANDOFLESSER_ASSET_PATH))
            {
                return HANDOFLESSER_ASSET_PATH;
            }

            return HANDOFLESSER_PACKAGE_PATH;
        }

    }
}
