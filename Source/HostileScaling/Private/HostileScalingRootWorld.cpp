#include "HostileScalingRootWorld.h"
#include "HostileScalingSubsystem.h"

URootGameWorld_HostileScaling::URootGameWorld_HostileScaling()
{
    bRootModule = true;
    ModSubsystems.Add(AHostileScalingSubsystem::StaticClass());
}
