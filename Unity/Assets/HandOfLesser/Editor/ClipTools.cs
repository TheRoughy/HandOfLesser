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
    class ClipTools
    {
        private static readonly Dictionary<string, AnimationClip> sClips
            = new Dictionary<string, AnimationClip>();
        private static AnimationClip sContainer;

        public static void beginGeneration()
        {
            sClips.Clear();
            sContainer = null;
        }

        public static void saveClip(AnimationClip clip, string savePath)
        {
            string clipName = Path.GetFileNameWithoutExtension(savePath);
            clip.name = clipName;
            clip.hideFlags = HideFlags.HideInHierarchy;

            // Keep the generated clips in one asset instead of creating hundreds of individual
            // files. The first clip is the main asset and every later clip is a named subasset.
            if (sContainer == null)
            {
                sContainer = clip;
                AssetDatabase.CreateAsset(clip, HOL.Resources.getAnimationContainerPath());
            }
            else
            {
                AssetDatabase.AddObjectToAsset(clip, sContainer);
            }
            sClips.Add(clipName, clip);
        }

        public static AnimationClip loadClip(string path)
        {
            string clipName = Path.GetFileNameWithoutExtension(path);
            if (sClips.TryGetValue(clipName, out AnimationClip clip))
            {
                return clip;
            }

            foreach (UnityEngine.Object asset in AssetDatabase.LoadAllAssetsAtPath(
                         HOL.Resources.getAnimationContainerPath()))
            {
                if (asset is AnimationClip loadedClip)
                {
                    sClips[loadedClip.name] = loadedClip;
                }
            }

            return sClips.TryGetValue(clipName, out clip) ? clip : null;
        }

        public static void setClipProperty(ref AnimationClip clip, string property, float value)
        {
            // Single keyframe
            AnimationUtility.SetEditorCurve(
                    clip,
                    EditorCurveBinding.FloatCurve(
                        string.Empty,
                        typeof(Animator),
                        property
                        ),
                AnimationCurve.Linear(0, value, 0, value));
        }

        public static void setClipProperty(ref AnimationClip clip, string property, AnimationCurve curve)
        {
            // Single keyframe
            AnimationUtility.SetEditorCurve(
                    clip,
                    EditorCurveBinding.FloatCurve(
                        string.Empty,
                        typeof(Animator),
                        property
                        ),
                curve);
        }

        public static void setClipProperty(ref AnimationClip clip, string property, float startTime, float startValue, float EndTime, float endValue)
        {
            // Single keyframe
            AnimationUtility.SetEditorCurve(
                    clip,
                    EditorCurveBinding.FloatCurve(
                        string.Empty,
                        typeof(Animator),
                        property
                        ),
                AnimationCurve.Linear(startTime, startValue, EndTime, endValue));
        }

        public static void setClipPropertyLocalRotation( ref AnimationClip clip, Transform avatarRoot, Transform trans, Quaternion value)
        {
            AnimationCurve curveX = new AnimationCurve();
            AnimationCurve curveY = new AnimationCurve();
            AnimationCurve curveZ = new AnimationCurve();
            AnimationCurve curveW = new AnimationCurve();

            curveX.AddKey(0f, value.x);
            curveY.AddKey(0f, value.y);
            curveZ.AddKey(0f, value.z);
            curveW.AddKey(0f, value.w);

            // I guess relative to avatar is fine
            string relativePath = AnimationUtility.CalculateTransformPath(trans, avatarRoot);

            clip.SetCurve(relativePath, typeof(Transform), "localRotation.x", curveX);
            clip.SetCurve(relativePath, typeof(Transform), "localRotation.y", curveY);
            clip.SetCurve(relativePath, typeof(Transform), "localRotation.z", curveZ);
            clip.SetCurve(relativePath, typeof(Transform), "localRotation.w", curveW);
        }

    }
}
