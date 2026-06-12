#include "HostileScalingRootInstance.h"
#include "HostileScalingConfig.h"

URootInstance_HostileScaling::URootInstance_HostileScaling()
{
    bRootModule = true;
    ModConfigurations.Add(UHostileScalingConfig::StaticClass());
}
