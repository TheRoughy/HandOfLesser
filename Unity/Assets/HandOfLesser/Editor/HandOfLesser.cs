using HOL;
using System;
using System.Collections.Generic;
using UnityEditor;
using UnityEditor.Animations;
using UnityEngine;
using VRC.SDK3.Avatars.ScriptableObjects;

public class HandOfLesserAnimationGenerator : EditorWindow
{
    static TransmitType sTransmitType = TransmitType.packed;

    static float sSmoothingTimeMS = 100.0f;
    static float sFullStepSmoothingTimeMS = 250.0f;
    static float sHalfStepSmoothingTimeMS = 500.0f;

    static GameObject sTargetAvatar = null;
    static bool sUseSkeletal = false;
    static bool sUseInterlace = true;

    [MenuItem("Window/HandOfLesser")]
    public static void ShowWindow()
    {
        GetWindow<HandOfLesserAnimationGenerator>("Hand of Lesser");
    }

    private void OnGUI()
    {
        sSmoothingTimeMS = Mathf.Max(
            0.0f,
            EditorGUILayout.FloatField(
                new GUIContent(
                    "Smoothing time (ms):",
                    "Time for networked finger movement to reach approximately 95% of its target. Use 0 to disable smoothing."),
                sSmoothingTimeMS));

        sFullStepSmoothingTimeMS = Mathf.Max(
            sSmoothingTimeMS,
            EditorGUILayout.FloatField(
                new GUIContent(
                    "Full-step smoothing time (ms):",
                    "Smoothing time used when consecutive networked targets differ by one full step."),
                sFullStepSmoothingTimeMS));

        sHalfStepSmoothingTimeMS = Mathf.Max(
            sFullStepSmoothingTimeMS,
            EditorGUILayout.FloatField(
                new GUIContent(
                    "Half-step smoothing time (ms):",
                    "Smoothing time used when consecutive networked targets differ by one interlaced half-step."),
                sHalfStepSmoothingTimeMS));

        sTargetAvatar = (GameObject) EditorGUILayout.ObjectField("Avatar:", sTargetAvatar, typeof(GameObject), true);

        // sUseSkeletal = EditorGUILayout.Toggle("Use skeletal: ", sUseSkeletal);

        if (GUILayout.Button("Generate Animations"))
        {
            generateAnimations(sTransmitType);
        }

        if (GUILayout.Button("Generate Controller"))
        {
            generateAnimationController(sTransmitType);
        }

        if (GUILayout.Button("Generate Parameters"))
        {
            generateAvatarParameters(sTransmitType);
        }
    }

    private static void addAnimatorLayer(AnimatorController controller, ControllerLayer layer, float weight, AvatarMask mask)
    {
        // https://forum.unity.com/threads/animator-controller-layer-and-default-weight.527167/
        // You cannot programtically modify a layer after it has been created, so in order to set a weight
        // and avatarMask, you must manually crate both the layer and its stateMachine, set your settings, then add it to the controller

        AnimatorStateMachine stateMachine = new AnimatorStateMachine
        {
            name = controller.MakeUniqueLayerName(layer.propertyName()),
            hideFlags = HideFlags.HideInHierarchy
        };

        controller.AddLayer(new AnimatorControllerLayer
        {
            stateMachine = stateMachine,
            name = stateMachine.name,
            defaultWeight = weight,

            avatarMask = mask
        });

        // Statemachine isn't saved otherwise?
        AssetDatabase.AddObjectToAsset(stateMachine, controller);
    }

    private static void generateControllerLayers(AnimatorController controller, TransmitType transmitType)
    {
        AvatarMask bothHandsMask = sUseSkeletal ? HOL.Resources.getBothHandsSkeletalMask() : HOL.Resources.getBothHandsMask();

        // We cannot modify the weight or mask of any layers added to the controller, 
        // so we need to remove the base layer, and manually create it and our finger joint layer
        controller.RemoveLayer(0);

        addAnimatorLayer(controller, ControllerLayer.baseLayer, 1, bothHandsMask);

        addAnimatorLayer(controller, ControllerLayer.fpsMeasure, 1, bothHandsMask);
        addAnimatorLayer(controller, ControllerLayer.fpsSmoothing, 1, bothHandsMask);
        addAnimatorLayer(controller, ControllerLayer.smoothingAdjustment, 1, bothHandsMask);

        switch (transmitType)
        {
            case TransmitType.alternating:
                addAnimatorLayer(controller, ControllerLayer.inputAlternating, 1, bothHandsMask);
                break;
            case TransmitType.packed:
                addAnimatorLayer(controller, ControllerLayer.inputPacked, 1, bothHandsMask);

                if (sUseInterlace)
                {
                    addAnimatorLayer(controller, ControllerLayer.interlaceWeigh, 1, bothHandsMask);
                    addAnimatorLayer(controller, ControllerLayer.interlateOutput, 1, bothHandsMask);
                    addAnimatorLayer(controller, ControllerLayer.targetHistory, 1, bothHandsMask);
                    addAnimatorLayer(controller, ControllerLayer.targetDifference, 1, bothHandsMask);
                    addAnimatorLayer(controller, ControllerLayer.smoothingMode, 1, bothHandsMask);
                }

                break;
            default:
                break;
        }

        // Local full bypasses the normal smoothing stack and copies its dedicated raw params
        // straight into smooth through this layer.
        addAnimatorLayer(controller, ControllerLayer.directInput, 1, bothHandsMask);
        addAnimatorLayer(controller, ControllerLayer.smoothing, 1, bothHandsMask);
        addAnimatorLayer(controller, ControllerLayer.bend, 1, bothHandsMask);
    }

    private static void populateControllerLayers(AnimatorController controller, TransmitType transmitType)
    {
        switch (transmitType)
        {
            case TransmitType.alternating:
                Alternating.populateHandSwitchLayer(controller);
                break;
            case TransmitType.packed:   // TODO generate unpacking layer
                Packed.populatePackedLayer(controller, sUseInterlace);
                if (sUseInterlace)
                {
                    InterlacedWeigh.populateWeighLayer(controller);
                    InterlacedCombine.populateCombineLayer(controller);
                    InterlacedBuffer.populateTargetHistoryLayer(controller);
                    InterlacedWeigh.populateTargetDifferenceLayer(controller);
                    Smoothing.populateSmoothingModeLayer(controller);
                }
                break;
            default:
                break;
        }
        FrameRateMeasure.populateFpsMeasureLayer(controller);
        FrameRateMeasure.populateFpsSmoothingLayer(controller);
        SmoothingAdjustment.populateSmoothingAdjustmentLayer(
            controller,
            sSmoothingTimeMS,
            sFullStepSmoothingTimeMS,
            sHalfStepSmoothingTimeMS);
        DirectInput.populateLayer(controller);
        Smoothing.populateSmoothingLayer(controller, transmitType == TransmitType.packed && sUseInterlace);
        FingerBend.populateFingerJointLayer(controller, sUseSkeletal);

        ProgressDisplay.clearProgress();
    }

    private void generateControllerParameters(AnimatorController controller, TransmitType transmitType)
    {
        // Add all the parameters we'll be working with, they need to be present here to be driven

        if (transmitType == TransmitType.alternating)
        {
            // Only if generating controller for alternating update
            controller.AddParameter(HOL.Resources.HAND_SIDE_OSC_PARAMETER_NAME, AnimatorControllerParameterType.Int); // left/right switch
        }

        controller.AddParameter(new AnimatorControllerParameter()
        {
            name = HOL.Resources.ALWAYS_1_PARAMETER,
            type = AnimatorControllerParameterType.Float,
            defaultFloat = 1
        });

        controller.AddParameter(new AnimatorControllerParameter()
        {
            name = HOL.Resources.ALWAYS_HALF_PARAMETER,
            type = AnimatorControllerParameterType.Float,
            defaultFloat = 0.5f
        });

        controller.AddParameter(new AnimatorControllerParameter()
        {
            name = HOL.Resources.USE_FULL_PARAMETER,
            type = AnimatorControllerParameterType.Int,
            defaultInt = 0
        });

        // Always generate the local full parameter set. Even when packed/alternating is the
        // network transport, the local avatar still needs a separate raw full source it can
        // consume without sharing ownership of input/smooth with the Animator.
        foreach (HandSide side in new HandSide().Values())
        {
            foreach (FingerType finger in new FingerType().Values())
            {
                foreach (FingerBendType joint in new FingerBendType().Values())
                {
                    controller.AddParameter(
                        HOL.Resources.getJointParameterName(side, finger, joint, PropertyType.OSC_Full),
                        AnimatorControllerParameterType.Float);
                }
            }
        }

        // Alternating / Packed raw transport parameters.
        if (transmitType != TransmitType.full)
        {
            foreach (HandSide side in new HandSide().Values())
            {
                foreach (FingerType finger in new FingerType().Values())
                {
                    foreach (FingerBendType joint in new FingerBendType().Values())
                    {
                        controller.AddParameter(HOL.Resources.getJointParameterName(side, finger, joint, transmitType.toPropertyType()), AnimatorControllerParameterType.Float);
                    }
                }

                // Alternating / Packed share a single value for each hand
                // If there's ever an additional OSC data form that requires creating blendtree parameters for both sides,
                // adjust the logic here accordingly.
                break;
            }
        }     

        // Input ( after decoding packed or alternating )
        foreach (HandSide side in new HandSide().Values())
        {
            foreach (FingerType finger in new FingerType().Values())
            {
                foreach (FingerBendType joint in new FingerBendType().Values())
                {
                    // left and right hand joints
                    controller.AddParameter(HOL.Resources.getJointParameterName(side, finger, joint, PropertyType.input), AnimatorControllerParameterType.Float);

                    // Smoothened proxy values
                    controller.AddParameter(HOL.Resources.getJointParameterName(side, finger, joint, PropertyType.smooth), AnimatorControllerParameterType.Float);
                }
            }
        }

        if (sUseInterlace)
        {
            InterlacedBuffer.addParameters(controller);
            InterlacedWeigh.addParameters(controller);
            Smoothing.addSmoothingModeParameters(controller);
        }

        FrameRateMeasure.addParameters(controller);
    }

    private void generateAnimationController(TransmitType transmitType)
    {
        HOL.Resources.createOutputDirectories();

        // New animation controller
        AnimatorController controller = AnimatorController.CreateAnimatorControllerAtPath(HOL.Resources.getAnimationControllerOutputPath());

        generateControllerParameters(controller, transmitType);
        generateControllerLayers(controller, transmitType);
        populateControllerLayers(controller, transmitType);

        AssetDatabase.SaveAssets();
    }

    private void generateAnimations(TransmitType transmitType)
    {
        if (sTargetAvatar == null)
        {
            EditorGUILayout.HelpBox("Not avatar assigned!", MessageType.Warning);
        }


        // https://forum.unity.com/threads/createasset-is-super-slow.291667/#post-3853330
        // Makes createAsset not be super slow. Note the call to StopAssetEditing() below.
        AssetDatabase.StartAssetEditing(); // Yes, Start goes first.

        try
        {
            FingerBend.generateAnimations(sTargetAvatar, sUseSkeletal);
            Smoothing.generateAnimations();
            switch (transmitType)
            {
                case TransmitType.packed:
                    {
                        Packed.generateAnimations(sUseInterlace);
                        if (sUseInterlace)
                        {
                            InterlacedWeigh.generateAnimations();
                            InterlacedCombine.generateAnimations();
                            InterlacedBuffer.generateAnimations();
                            Smoothing.generateSmoothingModeAnimations();
                        }
                        break;
                    }
                default:
                    break;
            }
        }
        catch (Exception ex)
        {
            Debug.LogException(ex);
        }
        finally
        {
            AssetDatabase.StopAssetEditing();
        }
    }


        private void generateAvatarParameters(TransmitType transmitType)
        {
            VRCExpressionParameters paramAsset = VRCExpressionParameters.CreateInstance<VRCExpressionParameters>();

        // Usually we'll need to add the parameters for full for local use,
        // and alternating or packed for network.
        // If this is the future and we have enough params to use full for both,
        // we should only add full. Otherwise, full should be added but without sync.
        List<PropertyType> propertyTypes = new List<PropertyType> { PropertyType.OSC_Full };
            bool syncFull = true;
            if (transmitType != TransmitType.full)
            {
                syncFull = false;   // Trnasmit type not full, do not sync full
                propertyTypes.Add(transmitType.toPropertyType());
            }

            List<VRCExpressionParameters.Parameter> parameters = new List<VRCExpressionParameters.Parameter>();

            parameters.Add(new VRCExpressionParameters.Parameter()
            {
                name = HOL.Resources.USE_FULL_PARAMETER,
                valueType = VRCExpressionParameters.ValueType.Int,
                defaultValue = 0,
                networkSynced = false
            });

            foreach (PropertyType propertyType in propertyTypes)
            {
                foreach(HandSide side in new HandSide().Values())
                {
                    foreach (FingerType finger in new FingerType().Values())
                    {
                        foreach (FingerBendType joint in new FingerBendType().Values())
                        {
                            // Curl
                            VRCExpressionParameters.Parameter newParam = new VRCExpressionParameters.Parameter();
                            newParam.name = HOL.Resources.getJointParameterName(side, finger, joint, propertyType);
                            newParam.valueType
                                = propertyType == PropertyType.OSC_Packed
                                  ? VRCExpressionParameters.ValueType.Int
                                  : VRCExpressionParameters.ValueType.Float;
                            newParam.defaultValue = 0;
                            // Only sync if not full, or syncFull true because we will be using full for network sync too
                            newParam.networkSynced = propertyType != PropertyType.OSC_Full || syncFull;

                            parameters.Add(newParam);
                        }
                    }

                    // As of writing, only Full has separate parameters for left/right hands
                    // Alternating and Packed share a single one.
                    if (propertyType != PropertyType.OSC_Full)
                        break;
                }
            }

            if (transmitType == TransmitType.packed)
            {
                if (sUseInterlace)
                {
                    // flipFlop for interlaced
                    VRCExpressionParameters.Parameter newParam = new VRCExpressionParameters.Parameter();
                    newParam.name = HOL.Resources.INTERLACE_BIT_OSC_PARAMETER_NAME;
                    newParam.valueType = VRCExpressionParameters.ValueType.Bool;
                    newParam.defaultValue = 0;
                    newParam.networkSynced = true;

                    parameters.Add(newParam);
                }
                    
            }

            if (transmitType == TransmitType.alternating)
            {
                // Hand side
                VRCExpressionParameters.Parameter newParam = new VRCExpressionParameters.Parameter();
                newParam.name = HOL.Resources.HAND_SIDE_OSC_PARAMETER_NAME;
                newParam.valueType = VRCExpressionParameters.ValueType.Bool;
                newParam.defaultValue = 0;
                newParam.networkSynced = true;

                parameters.Add(newParam);
            }

            paramAsset.parameters = parameters.ToArray();

            AssetDatabase.CreateAsset(paramAsset, HOL.Resources.getParametersPath());
            AssetDatabase.SaveAssets();
        }
    }
