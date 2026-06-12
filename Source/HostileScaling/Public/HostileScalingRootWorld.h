#pragma once

#include "CoreMinimal.h"
#include "Module/GameWorldModule.h"
#include "HostileScalingRootWorld.generated.h"

// Root game world module. Spawns the scaling subsystem into each game world.
UCLASS()
class HOSTILESCALING_API URootGameWorld_HostileScaling : public UGameWorldModule
{
    GENERATED_BODY()

public:
    URootGameWorld_HostileScaling();
};
