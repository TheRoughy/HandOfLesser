#if HOL_MODULAR_AVATAR
using System.Collections.Generic;
using nadena.dev.modular_avatar.core;
using UnityEditor;
using UnityEditor.Animations;
using UnityEngine;
using VRC.SDK3.Avatars.Components;
using VRC.SDK3.Avatars.ScriptableObjects;

namespace HOL
{
    public static class ModularAvatarPackageBuilder
    {
        private const string PrefabName = "HandOfLesser";
        private const int GestureLayerPriority = 100;

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
            if (controller == null || expressionParameters == null)
            {
                Debug.LogError("HandOfLesser generated assets could not be loaded.");
                return;
            }

            GameObject prefabRoot = new GameObject(PrefabName);
            try
            {
                configureMergeAnimator(prefabRoot, controller);
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
            AnimatorController controller)
        {
            ModularAvatarMergeAnimator mergeAnimator
                = prefabRoot.AddComponent<ModularAvatarMergeAnimator>();
            mergeAnimator.animator = controller;
            mergeAnimator.layerType = VRCAvatarDescriptor.AnimLayerType.Gesture;
            mergeAnimator.pathMode = MergeAnimatorPathMode.Absolute;
            mergeAnimator.layerPriority = GestureLayerPriority;
            mergeAnimator.mergeAnimatorMode = MergeAnimatorMode.Append;
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
