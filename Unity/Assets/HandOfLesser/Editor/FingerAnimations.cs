using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using UnityEngine;

namespace HOL.FingerAnimations
{
    
    interface FingerAnimationInterface
    {
        int generateBendAnimation(HandSide side, FingerType finger, FingerBendType joint, AnimationClipPosition position);
        int generateCombinedCurlSplayAnimation(HandSide side, FingerType finger, AnimationClipPosition curlPosition, AnimationClipPosition splayPosition);
    }

    class HumanoidFingerAnimations : FingerAnimationInterface
    {
        public int generateBendAnimation(HandSide side, FingerType finger, FingerBendType joint, AnimationClipPosition position)
        {
            AnimationClip clip = new AnimationClip();
            ClipTools.setClipProperty(
                ref clip,
                    HOL.Resources.getJointParameterName(side, finger, joint, PropertyType.avatarRig),
                    AnimationValues.getHumanoidValue(finger, joint, position)
                ); ;

            ClipTools.saveClip(clip, HOL.Resources.getAnimationOutputPath(HOL.Resources.getAnimationClipName(side, finger, joint, PropertyType.avatarRig, position)));

            return 1;
        }

        public int generateCombinedCurlSplayAnimation(HandSide side, FingerType finger, AnimationClipPosition curlPosition, AnimationClipPosition splayPosition)
        {
            AnimationClip clip = new AnimationClip();
            ClipTools.setClipProperty(
                ref clip,
                    HOL.Resources.getJointParameterName(side, finger, FingerBendType.first, PropertyType.avatarRig),
                    AnimationValues.getHumanoidValue(finger, FingerBendType.first, curlPosition)
                );

            ClipTools.setClipProperty(
            ref clip,
                HOL.Resources.getJointParameterName(side, finger, FingerBendType.splay, PropertyType.avatarRig),
                AnimationValues.getHumanoidValue(finger, FingerBendType.splay, splayPosition)
            );

            ClipTools.saveClip(clip, HOL.Resources.getAnimationOutputPath(HOL.Resources.getAnimationClipName(side, finger, FingerBendType.first, PropertyType.avatarRigCombined, curlPosition, splayPosition)));

            return 1;
        }
    }

    class SkeletalFingerAnimations : FingerAnimationInterface
    {
        private readonly GameObject mAvatar;

        public SkeletalFingerAnimations(GameObject avatar)
        {
            mAvatar = avatar;
        }

         private static Quaternion applyXRotation(Quaternion source, float curlRot)
        {
            return source * Quaternion.Euler(curlRot, 0, 0);
        }

        private static Quaternion applyZRotation(Quaternion source, float splayRot)
        {
            return source * Quaternion.Euler(0, 0, splayRot);
        }

        private static Quaternion applyYRotation(Quaternion source, float splayRot)
        {
            return source * Quaternion.Euler(0, splayRot, 0);
        }

        private static Quaternion applyRotation(Quaternion source, float curlRot, float splayRot)
        {
            // Get the correct splay by applying it first, followed by curl
            // Always Z for splay, X for curl
            Quaternion splayed = source * Quaternion.Euler(0, 0, splayRot);
            return splayed * Quaternion.Euler(curlRot, 0, 0);
        }

        private static Quaternion applyCurledInnerBend(Quaternion source, float bend)
        {
            return source * Quaternion.Euler(0, 0, bend);
        }


        public int generateBendAnimation(HandSide side, FingerType finger, FingerBendType joint, AnimationClipPosition position)
        {
            Transform fingerTrans = HOL.AvatarTools.getFingerBone(mAvatar, side, finger, joint);
            float relativeCurl = AnimationValues.getSkeletalValueFromCenter(side, finger, joint, position);

            // Absolute rotation for now, need some kind of calibration system to do this properly.
            Quaternion baseRot =  Quaternion.identity; // fingerTrans.localRotation;

            Quaternion curlRot = applyXRotation(baseRot, relativeCurl);

            AnimationClip clip = new AnimationClip();
            ClipTools.setClipPropertyLocalRotation(
                ref clip,
                    mAvatar.transform,
                    fingerTrans,
                    curlRot
                );

            ClipTools.saveClip(clip, HOL.Resources.getAnimationOutputPath(HOL.Resources.getAnimationClipName(side, finger, joint, PropertyType.avatarRig, position)));

            return 1;
        }

        public int generateCombinedCurlSplayAnimation(HandSide side, FingerType finger, AnimationClipPosition curlPosition, AnimationClipPosition splayPosition )
        {
            AnimationClip clip = new AnimationClip();
            Transform fingerTrans = HOL.AvatarTools.getFingerBone(mAvatar, side, finger, FingerBendType.first);

            float relativeCurl = AnimationValues.getSkeletalValueFromCenter(side, finger, FingerBendType.first, curlPosition);
            float relativeSplay = AnimationValues.getSkeletalValueFromCenter(side, finger, FingerBendType.splay, splayPosition);



            Quaternion rot;

            rot = applyRotation(fingerTrans.localRotation, relativeCurl, relativeSplay);
            
            if (curlPosition == AnimationClipPosition.positive && splayPosition == AnimationClipPosition.negative)
            {
                // curled, splayed inwards
                //rot = applyXRotation(fingerTrans.localRotation, relativeCurl);

                // Apply splay to z ( left right ) and y ( roll, when curled )
                rot = applyZRotation(rot, -relativeSplay);
                //rot = applyYRotation(rot, relativeSplay);

            }
            else
            {
               // rot = applyRotation(fingerTrans.localRotation, relativeCurl, relativeSplay);

            }

            /*
            // fine adjustment
            if(curlPosition == AnimationClipPosition.positive && splayPosition == AnimationClipPosition.positive)
            {
                rot = applyCurledInnerBend(rot, 20.0f);
            }*/

            ClipTools.setClipPropertyLocalRotation(
                ref clip,
                    mAvatar.transform,
                    fingerTrans,
                    rot
                );

            ClipTools.saveClip(clip, HOL.Resources.getAnimationOutputPath(HOL.Resources.getAnimationClipName(side, finger, FingerBendType.first, PropertyType.avatarRigCombined, curlPosition, splayPosition)));

            return 1;
        }

    }

   
}
