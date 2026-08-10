using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using UnityEditor.Animations;

namespace HOL
{
    enum FingerType
    {
        index,
        middle,
        ring,
        pinky,
        thumb
    }

    enum FingerBendType
    {
        first,
        second,
        third,
        splay
    }

    enum HandSide
    {
        left,
        right
    }

    enum ControllerLayer
    {
        baseLayer, 
        inputAlternating, 
        inputPacked, 
        directInput, // Copies raw full input to smooth when local full is enabled
        smoothing, 
        interlaceWeigh,
        interlateOutput, 
        bend,
        fpsMeasure,
        fpsSmoothing,
        smoothingAdjustment,
    }

    enum PropertyType
    {
        OSC_Full,       // Local-only raw full input. Do not reuse internal working params for this.
        OSC_Alternating,      
        OSC_Packed,     // We will use packed for network and full for local, so need to separate them
        input_packed,       // used for animation name because we need to do non-standard values
        input,              // full joint path with left/right qualified
        input_interlaced,           // same as above, but values can alternate to signify in-between value
        input_interlaced_first,     // Used to store prev/current interlaced value
        input_interlaced_second,    // we flip-flop between the two, so either may be current or prev.
        interlaced_weight,          // distance between current and past interlaced input
        smooth,     // Output used for smoothing, used to drive avatarRig
        avatarRig,  // Humanoid rig or skeletal
        avatarRigCombined,   // Contains both curl and splay in a single animation
        fps_time,       // Continuously animated clock used to measure frame time
        fps_previous,   // Clock value retained from the previous Animator evaluation
        fps,            // Raw time between Animator evaluations
        fps_smooth,     // Measured frame time is unstable, so smooth it before use
        smoothing_adjusted, // Per-frame retention adjusted to the current frame time
    }

    enum AnimationClipPosition
    {
        negative, neutral, positive
    }

    enum TransmitType
    {
        full,
        alternating,
        packed
    }

    static class HandOfLesserExtensions
    {
        /////////////
        // TransmitType
        ////////////

        public static PropertyType toPropertyType(this TransmitType transmitType)
        {
            // In the editor there is a space, but the actual attribute does not have one. Unity what the hell.
            switch (transmitType)
            {
                case TransmitType.full: return PropertyType.OSC_Full;
                case TransmitType.alternating: return PropertyType.OSC_Alternating;
                case TransmitType.packed: return PropertyType.OSC_Packed;
                default:
                    return PropertyType.OSC_Full;
            }
        }

        ///////////////////////////
        // Hand
        ///////////////////////////

        public static string propertyName(this HandSide side)
        {
            // In the editor there is a space, but the actual attribute does not have one. Unity what the hell.
            switch (side)
            {
                case HandSide.left: return "LeftHand";
                case HandSide.right: return "RightHand";
                default: return "INVALID_ENUM";
            }
        }

        ///////////////////////////
        // Finger
        ///////////////////////////

        public static string propertyName(this FingerType finger)
        {
            switch (finger)
            {
                case FingerType.index: return "Index";
                case FingerType.middle: return "Middle";
                case FingerType.ring: return "Ring";
                case FingerType.pinky: return "Little";
                case FingerType.thumb: return "Thumb";
                default: return "INVALID_ENUM";
            }
        }

        ///////////////////////////
        // Joint
        ///////////////////////////

        public static string propertyName(this FingerBendType joint)
        {
            switch (joint)
            {
                case FingerBendType.first: return "1";
                case FingerBendType.second: return "2";
                case FingerBendType.third: return "3";
                case FingerBendType.splay: return "splay";
                default: return "INVALID_ENUM";
            }
        }

        ///////////////////////////
        // AnimationClipPosition
        ///////////////////////////

        public static string propertyName(this AnimationClipPosition pos)
        {
            switch (pos)
            {
                case AnimationClipPosition.negative: return "Negative";
                case AnimationClipPosition.neutral: return "Neutral";
                case AnimationClipPosition.positive: return "Positive";
                default: return "INVALID_ENUM";
            }
        }

        /////////////////////
        // ControllerLayer
        /////////////////////

        public static string propertyName(this ControllerLayer prop)
        {
            switch (prop)
            {
                case ControllerLayer.baseLayer: return "HOL_base";
                case ControllerLayer.inputPacked:       return "HOL_inputPacked";
                case ControllerLayer.inputAlternating:  return "HOL_inputAlternating";
                case ControllerLayer.directInput:       return "HOL_directInput";
                case ControllerLayer.smoothing:         return "HOL_smoothing";
                case ControllerLayer.bend:              return "HOL_bend";
                case ControllerLayer.interlaceWeigh:    return "HOL_interlaceWeigh";
                case ControllerLayer.interlateOutput:   return "HOL_interlaceOutput";
                case ControllerLayer.fpsMeasure:        return "HOL_fpsMeasure";
                case ControllerLayer.fpsSmoothing:      return "HOL_fpsSmoothing";
                case ControllerLayer.smoothingAdjustment: return "HOL_smoothingAdjustment";
                default:
                    return "";
            }
        }

        public static AnimatorControllerLayer findLayer(this ControllerLayer prop, AnimatorController controller)
        {
            foreach (AnimatorControllerLayer layer in controller.layers)
            {
                if (layer.name.Equals(prop.propertyName()))
                {
                    return layer;
                }
            }

            return null;
        }

        /////////////////////
        // PropertyType
        /////////////////////
        public static string propertyName(this PropertyType prop)
        {
            switch (prop)
            {
                case PropertyType.OSC_Full: return "full"; // Raw local full path, kept separate from Animator-owned params
                case PropertyType.OSC_Alternating: return "alternating";
                case PropertyType.OSC_Packed: return "packed";
                case PropertyType.input: return "input";
                case PropertyType.input_packed: return "input"; // only used for animation clip names, drive input
                case PropertyType.smooth: return "smooth";
                case PropertyType.avatarRig: return "avatarRig";
                case PropertyType.avatarRigCombined: return "avatarRigCombined";
                case PropertyType.input_interlaced: return "inputInterlaced";
                case PropertyType.input_interlaced_first: return "inputInterlacedFirst";
                case PropertyType.input_interlaced_second: return "inputInterlacedSecond";
                case PropertyType.interlaced_weight: return "interlacedWeight";
                case PropertyType.fps_time: return "fpsTime";
                case PropertyType.fps_previous: return "fpsPrevious";
                case PropertyType.fps: return "fps";
                case PropertyType.fps_smooth: return "fpsSmooth";
                case PropertyType.smoothing_adjusted: return "smoothingAdjusted";
                default:
                    return "";
            }
        }

        public static string addPrefixNamespace(this PropertyType prop, string str)
        {
            return prop.propertyName() + "/" + str;
        }

        public static List<Enum> Values(this Enum theEnum)
        {
            return Enum.GetValues(theEnum.GetType()).Cast<Enum>().ToList();
        }

        public static AnimationClipPosition opposite(this AnimationClipPosition pos)
        {
            switch (pos)
            {
                case AnimationClipPosition.negative: return AnimationClipPosition.positive;
                case AnimationClipPosition.positive: return AnimationClipPosition.neutral;
                default: return AnimationClipPosition.neutral;
            }
        }
    }



}
