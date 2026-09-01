#if HOL_MODULAR_AVATAR
using System.Collections.Generic;
using nadena.dev.modular_avatar.core;
using UnityEditor;
using UnityEditor.Animations;
using UnityEngine;
using VRC.SDK3.Avatars.Components;
using VRC.SDK3.Avatars.ScriptableObjects;
using VRC.SDKBase;

namespace HOL
{
    public static class ModularAvatarPackageBuilder
    {
        private const string PrefabName = "HandOfLesser";
        private const string FingerTrackingOwnershipStateName
            = "HandOfLesserFingerOwnership";
        private const int GestureLayerPriority = 100;
        private const int FxLayerPriority = 100;

        [MenuItem("Tools/HandOfLesser/Build Modular Avatar Package Assets")]
        public static void BuildPackageAssets()
        {
            if (!HandOfLesserAnimationGenerator.GenerateAll())
            {
                return;
            }

            AnimatorController controller = AssetDatabase.LoadAssetAtPath<AnimatorController>(
                HandOfLesserAnimationGenerator.GetAnimationControllerOutputPath());
            VRCExpressionParameters expressionParameters
                = AssetDatabase.LoadAssetAtPath<VRCExpressionParameters>(
                    HandOfLesserAnimationGenerator.GetParametersOutputPath());
            AnimatorController fingerTrackingController = createFingerTrackingController();
            if (controller == null
                || expressionParameters == null
                || fingerTrackingController == null)
            {
                Debug.LogError("HandOfLesser generated assets could not be loaded.");
                return;
            }

            GameObject prefabRoot = new GameObject(PrefabName);
            try
            {
                configureMergeAnimator(
                    prefabRoot,
                    controller,
                    VRCAvatarDescriptor.AnimLayerType.Gesture,
                    GestureLayerPriority);
                configureFingerTrackingOwnership(prefabRoot, fingerTrackingController);
                configureParameters(prefabRoot, expressionParameters);

                GameObject prefab = PrefabUtility.SaveAsPrefabAsset(
                    prefabRoot,
                    HandOfLesserAnimationGenerator.GetModularAvatarPrefabOutputPath());
                if (prefab == null)
                {
                    Debug.LogError("HandOfLesser could not create its Modular Avatar prefab.");
                    return;
                }
            }
            finally
            {
                Object.DestroyImmediate(prefabRoot);
            }

            // The prefab stores Modular Avatar's parameter declarations directly, so the
            // intermediate VRChat parameter asset is not part of the distributable package.
            AssetDatabase.DeleteAsset(HandOfLesserAnimationGenerator.GetParametersOutputPath());
            AssetDatabase.SaveAssets();
            AssetDatabase.Refresh();
            Debug.Log("Generated HandOfLesser Modular Avatar package assets.");
        }

        private static void configureMergeAnimator(
            GameObject prefabRoot,
            AnimatorController controller,
            VRCAvatarDescriptor.AnimLayerType layerType,
            int layerPriority)
        {
            ModularAvatarMergeAnimator mergeAnimator
                = prefabRoot.AddComponent<ModularAvatarMergeAnimator>();
            mergeAnimator.animator = controller;
            mergeAnimator.layerType = layerType;
            mergeAnimator.pathMode = MergeAnimatorPathMode.Absolute;
            mergeAnimator.layerPriority = layerPriority;
            mergeAnimator.mergeAnimatorMode = MergeAnimatorMode.Append;
        }

        private static AnimatorController createFingerTrackingController()
        {
            AnimatorController controller = AnimatorController.CreateAnimatorControllerAtPath(
                HandOfLesserAnimationGenerator.GetFingerTrackingControllerOutputPath());
            if (controller == null)
            {
                return null;
            }

            AnimatorControllerLayer layer = controller.layers[0];
            layer.name = "HOLFingerTrackingOwnership";

            controller.AddParameter("TrackingType", AnimatorControllerParameterType.Int);

            AnimatorState waitForTracking = layer.stateMachine.AddState("WaitForTracking");
            waitForTracking.writeDefaultValues = false;
            layer.stateMachine.defaultState = waitForTracking;

            AnimatorState ownership = layer.stateMachine.AddState(
                FingerTrackingOwnershipStateName);
            ownership.writeDefaultValues = false;

            // VRChat initializes skeletal finger tracking after the avatar animator starts. Wait
            // until tracking is initialized before claiming the finger muscles for animation.
            AnimatorStateTransition claimOwnership = waitForTracking.AddTransition(ownership);
            claimOwnership.hasExitTime = false;
            claimOwnership.hasFixedDuration = true;
            claimOwnership.duration = 0;
            claimOwnership.AddCondition(
                AnimatorConditionMode.Greater,
                2,
                "TrackingType");

            // VRChat's skeletal input owns finger muscles by default. This state tells it to leave
            // those muscles under animator control instead.
            VRCAnimatorTrackingControl tracking
                = ownership.AddStateMachineBehaviour<VRCAnimatorTrackingControl>();
            tracking.trackingLeftFingers
                = VRC_AnimatorTrackingControl.TrackingType.Animation;
            tracking.trackingRightFingers
                = VRC_AnimatorTrackingControl.TrackingType.Animation;

            return controller;
        }

        private static void configureFingerTrackingOwnership(
            GameObject prefabRoot,
            AnimatorController fingerTrackingController)
        {
            // Only finger ownership lives in FX. The hand-masked Gesture layer remains responsible
            // for all humanoid animation.
            GameObject ownershipRoot = new GameObject("Finger Tracking Ownership");
            ownershipRoot.transform.SetParent(prefabRoot.transform, false);
            configureMergeAnimator(
                ownershipRoot,
                fingerTrackingController,
                VRCAvatarDescriptor.AnimLayerType.FX,
                FxLayerPriority);
        }

        private static void configureParameters(
            GameObject prefabRoot,
            VRCExpressionParameters expressionParameters)
        {
            ModularAvatarParameters modularParameters
                = prefabRoot.AddComponent<ModularAvatarParameters>();
            List<ParameterConfig> parameters = new List<ParameterConfig>();

            foreach (VRCExpressionParameters.Parameter parameter in expressionParameters.parameters)
            {
                parameters.Add(new ParameterConfig
                {
                    nameOrPrefix = parameter.name,
                    syncType = getSyncType(parameter.valueType),
                    localOnly = !parameter.networkSynced,
                    defaultValue = parameter.defaultValue,
                    saved = parameter.saved,
                    hasExplicitDefaultValue = true
                });
            }

            modularParameters.parameters = parameters;
        }

        private static ParameterSyncType getSyncType(
            VRCExpressionParameters.ValueType valueType)
        {
            switch (valueType)
            {
                case VRCExpressionParameters.ValueType.Bool:
                    return ParameterSyncType.Bool;
                case VRCExpressionParameters.ValueType.Int:
                    return ParameterSyncType.Int;
                default:
                    return ParameterSyncType.Float;
            }
        }
    }
}
#endif
