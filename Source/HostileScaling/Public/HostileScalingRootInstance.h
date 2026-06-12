#pragma once

#include "CoreMinimal.h"
#include "Module/GameInstanceModule.h"
#include "HostileScalingRootInstance.generated.h"

// Root game instance module. SML discovers it automatically because
// bRootModule is true. Registers the mod configuration.
UCLASS()
class HOSTILESCALING_API URootInstance_HostileScaling : public UGameInstanceModule
{
    GENERATED_BODY()

public:
    URootInstance_HostileScaling();
};
