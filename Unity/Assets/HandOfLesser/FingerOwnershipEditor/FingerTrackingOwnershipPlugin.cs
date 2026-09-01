#if HOL_MODULAR_AVATAR
using System.Linq;
using nadena.dev.ndmf;
using nadena.dev.ndmf.animator;
using VRC.SDK3.Avatars.Components;
using VRC.SDKBase;

[assembly: ExportsPlugin(typeof(HOL.FingerTrackingOwnershipPlugin))]

namespace HOL
{
    internal sealed class FingerTrackingOwnershipPlugin
        : Plugin<FingerTrackingOwnershipPlugin>
    {
        private const string OwnershipStateName = "HandOfLesserFingerOwnership";

        public override string QualifiedName => "com.nordskog.handoflesser.finger-ownership";
        public override string DisplayName => "HandOfLesser Finger Ownership";

        protected override void Configure()
        {
            InPhase(BuildPhase.Transforming)
                .WithRequiredExtension(typeof(AnimatorServicesContext), sequence =>
                {
                    sequence
                        .AfterPlugin("nadena.dev.modular-avatar")
                        .Run("Preserve HandOfLesser finger ownership", preserveFingerOwnership);
                });
        }

        private static void preserveFingerOwnership(BuildContext context)
        {
            VirtualControllerContext controllers
                = context.Extension<AnimatorServicesContext>().ControllerContext;

            // Only modify avatars containing the ownership state generated for this prefab.
            bool hasOwnershipState = controllers.GetAllControllers()
                .SelectMany(controller => controller.Layers)
                .Where(layer => layer.StateMachine != null)
                .SelectMany(layer => layer.StateMachine.AllStates())
                .Any(state => state.Name == OwnershipStateName);
            if (!hasOwnershipState)
            {
                return;
            }

            foreach (VirtualAnimatorController controller in controllers.GetAllControllers())
            {
                foreach (VirtualLayer layer in controller.Layers)
                {
                    if (layer.StateMachine == null)
                    {
                        continue;
                    }

                    foreach (VirtualState state in layer.StateMachine.AllStates())
                    {
                        bool ownsFingers = state.Name == OwnershipStateName;
                        VRC_AnimatorTrackingControl.TrackingType fingerMode = ownsFingers
                            ? VRC_AnimatorTrackingControl.TrackingType.Animation
                            : VRC_AnimatorTrackingControl.TrackingType.NoChange;

                        // Existing Action states may still control the rest of the body, but they
                        // must not restore skeletal ownership of the finger muscles.
                        foreach (VRCAnimatorTrackingControl tracking in state.Behaviours
                            .OfType<VRCAnimatorTrackingControl>())
                        {
                            tracking.trackingLeftFingers = fingerMode;
                            tracking.trackingRightFingers = fingerMode;
                        }
                    }
                }
            }
        }
    }
}
#endif
