#include "HostileScalingSubsystem.h"
#include "HostileScaling.h"
#include "HostileScalingGlobals.h"
#include "HostileScalingConfig.h"
#include "HostileScalingBuildMarker.h"

#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "FGSchematicManager.h"
#include "Creature/FGCreature.h"
#include "Creature/FGCreatureSpawner.h"

// MCP contract v1 (_team/mcp-mod-contract-v1.md): McpDescribe + McpSnapshot.
// READ-ONLY by rule: nothing here writes to this subsystem, any spawner, any
// creature, the config, or the HostileScaling:: globals; no timers, no
// spawning, and at most one Verbose log line per McpSnapshot call. JSON is
// hand-built (no Json module dependency) and pure ASCII, so FString length
// equals the UTF-8 byte count the caller receives.

namespace
{
    constexpr int32 McpDefaultRows = 25;
    constexpr int32 McpMaxRows = 100;             // hard clamp for count (spawners and nearest)
    constexpr int32 McpRowBudgetChars = 56000;    // rows stop here; the envelope stays well under 64 KB
    constexpr int32 McpMaxStringChars = 512;      // per string value, before escaping
    constexpr int32 McpMaxArgsChars = 1024;
    constexpr int32 McpEchoChars = 64;             // caller text echoed into error objects (contract R3.3)
    // Copy of RefreshIntervalSeconds (HostileScalingSubsystem.cpp:17, file-local there and that file is
    // frozen): every config key is re-read on that timer. KEEP IN SYNC by hand.
    constexpr int32 McpRefreshIntervalSeconds = 10;

    // JSON string literal: escapes quote, backslash and control chars; every
    // non-ASCII char becomes a \u escape so the output stays ASCII.
    FString JStr(const FString& In)
    {
        FString Out(TEXT("\""));
        int32 Len = FMath::Min(In.Len(), McpMaxStringChars);
        if (Len < In.Len() && Len > 0 && In[Len - 1] >= 0xD800 && In[Len - 1] <= 0xDBFF) { --Len; }
        for (int32 i = 0; i < Len; ++i)
        {
            const TCHAR C = In[i];
            if (C == TEXT('"')) { Out += TEXT("\\\""); }
            else if (C == TEXT('\\')) { Out += TEXT("\\\\"); }
            else if (C < 0x20 || C > 0x7E) { Out += FString::Printf(TEXT("\\u%04x"), static_cast<uint32>(C) & 0xFFFF); }
            else { Out.AppendChar(C); }
        }
        if (In.Len() > Len)
        {
            Out += TEXT("...");
        }
        Out += TEXT("\"");
        return Out;
    }

    FString JNum(double V)
    {
        return FMath::IsFinite(V) ? FString::Printf(TEXT("%.4f"), V) : FString(TEXT("null"));
    }

    const TCHAR* JBool(bool b) { return b ? TEXT("true") : TEXT("false"); }

    // Caller-supplied text echoed into an error: clipped to McpEchoChars, never splitting a surrogate pair.
    FString ClipEcho(const FString& In)
    {
        if (In.Len() <= McpEchoChars) { return In; }
        int32 Len = McpEchoChars;
        if (In[Len - 1] >= 0xD800 && In[Len - 1] <= 0xDBFF) { --Len; }
        return In.Left(Len) + TEXT("...");
    }

    // TODO(2026-11-09): duplicate of SpawnerIsHostile; fold into one private static member when HostileScalingSubsystem.cpp is next unfrozen.
    // Copy of SpawnerIsHostile (HostileScalingSubsystem.cpp:23-36, anonymous
    // namespace there, so not callable here). Same predicate, except the CDO is
    // fetched WITHOUT creating it: 0 hostile, 1 passive, 2 null class, 3 class
    // present but no CDO (the original would create one and test it).
    int32 ClassifySpawner(const AFGCreatureSpawner* Spawner)
    {
        const TSubclassOf<AFGCreature> CreatureClass = Spawner->GetCreatureClass();
        if (!CreatureClass)
        {
            return 2;
        }
        const AFGCreature* CDO = Cast<AFGCreature>(CreatureClass->GetDefaultObject(false));
        if (!CDO)
        {
            return 3;
        }
        return CDO->IsPassiveCreature() ? 1 : 0;
    }

    // Strict parser for a FLAT JSON object whose values are strings or integers.
    // Returns false with OutError on anything else.
    bool ParseFlatArgs(const FString& In, TMap<FString, FString>& OutStr, TMap<FString, int32>& OutInt, FString& OutError)
    {
        int32 i = 0;
        const int32 N = In.Len();
        auto SkipWs = [&]() { while (i < N && FChar::IsWhitespace(In[i])) { ++i; } };
        auto ReadString = [&](FString& Out) -> bool
        {
            if (i >= N || In[i] != TEXT('"')) { return false; }
            ++i;
            while (i < N && In[i] != TEXT('"'))
            {
                if (In[i] == TEXT('\\'))
                {
                    if (i + 1 >= N || In[i + 1] == TEXT('u')) { return false; }
                    ++i;
                }
                Out.AppendChar(In[i]);
                ++i;
            }
            if (i >= N) { return false; }
            ++i;
            return true;
        };

        SkipWs();
        if (i >= N || In[i] != TEXT('{')) { OutError = TEXT("args is not a JSON object"); return false; }
        ++i;
        SkipWs();
        if (i < N && In[i] == TEXT('}')) { ++i; }
        else
        {
            while (true)
            {
                FString Key;
                SkipWs();
                if (!ReadString(Key)) { OutError = TEXT("expected a quoted key"); return false; }
                SkipWs();
                if (i >= N || In[i] != TEXT(':')) { OutError = TEXT("expected a colon after a key"); return false; }
                ++i;
                SkipWs();
                if (i < N && In[i] == TEXT('"'))
                {
                    FString Value;
                    if (!ReadString(Value)) { OutError = TEXT("unterminated or unsupported string value"); return false; }
                    OutStr.Add(Key, Value);
                }
                else
                {
                    const int32 Start = i;
                    if (i < N && In[i] == TEXT('-')) { ++i; }
                    while (i < N && FChar::IsDigit(In[i])) { ++i; }
                    const int32 Digits = i - Start - ((Start < N && In[Start] == TEXT('-')) ? 1 : 0);
                    if (Digits < 1 || Digits > 9) { OutError = TEXT("values must be strings or integers of at most 9 digits"); return false; }
                    OutInt.Add(Key, FCString::Atoi(*In.Mid(Start, i - Start)));
                }
                SkipWs();
                if (i < N && In[i] == TEXT(',')) { ++i; continue; }
                if (i < N && In[i] == TEXT('}')) { ++i; break; }
                OutError = TEXT("expected a comma or closing brace");
                return false;
            }
        }
        SkipWs();
        if (i != N) { OutError = TEXT("trailing characters after the object"); return false; }
        return true;
    }

    // Shared envelope head: contract, mod, marker. Caller appends and closes.
    FString McpHead()
    {
        return FString(TEXT("{\"contract\":1,\"mod\":\"HostileScaling\",\"buildMarker\":\"")) +
            TEXT(HOSTILESCALING_BUILD_MARKER) + TEXT("\"");
    }

    FString McpError(const TCHAR* Kind, const FString& Message)
    {
        UE_LOG(LogHostileScaling, Verbose, TEXT("[MCP] McpSnapshot error %s: %s"), Kind, *Message.Left(256));
        return McpHead() + TEXT(",\"errorKind\":") + JStr(Kind) + TEXT(",\"error\":") + JStr(Message) + TEXT("}");
    }
}

FString AHostileScalingSubsystem::McpDescribe() const
{
    // Static manifest: no world, config or actor access, so it is safe before
    // BeginPlay and in the menu. Every key is re-read by RefreshScaling on the
    // 10 s timer (HostileScalingSubsystem.cpp:17,81-83,202); classifications and
    // proving lines are in _team/test-logs/2026-11-09-hostilescaling-mcp1-handoff.md.
    // SaveEffect per contract R2.1 (none | gameplay | structural); grades and proving lines in
    // _team/test-logs/2026-11-10-hostilescaling-mcp2-handoff.md. Unsure means structural.
    struct FKeyRow { const TCHAR* Key; bool bTouchesSave; const TCHAR* SaveEffect; const TCHAR* Note; };
    static const FKeyRow Keys[] = {
        {TEXT("Enabled"), true, TEXT("structural"), TEXT("Master switch. Off: published health and damage multipliers become 1, so hostile untamed creatures that begin play with scaled health are written back to vanilla health, and, when the keeper was on, loaded spawners have radii restored and near-base re-traced (a SaveGame flag). On: every write below resumes.")},
        {TEXT("HealthBase"), true, TEXT("gameplay"), TEXT("Health multiplier input. Writes max and current health (SaveGame fields) on live hostile creatures, recomputed from the class default each sweep.")},
        {TEXT("HealthPerTier"), true, TEXT("gameplay"), TEXT("Health multiplier input, per unlocked milestone tier. Same writes as HealthBase.")},
        {TEXT("HealthMax"), true, TEXT("gameplay"), TEXT("Health multiplier cap. Same writes as HealthBase.")},
        {TEXT("DamageBase"), true, TEXT("structural"), TEXT("Damage multiplier input. The damage hooks scale damage hostile creatures deal to players; the game then applies it to SaveGame health.")},
        {TEXT("DamagePerTier"), true, TEXT("structural"), TEXT("Damage multiplier input, per unlocked milestone tier. Same effect as DamageBase.")},
        {TEXT("DamageMax"), true, TEXT("structural"), TEXT("Damage multiplier cap. Same effect as DamageBase.")},
        {TEXT("ScaleDamageToBuildings"), true, TEXT("structural"), TEXT("Extends the damage hooks to non-player targets, so scaled creature damage also reaches building and vehicle health.")},
        {TEXT("SpawnBase"), true, TEXT("structural"), TEXT("Spawn multiplier input. Sets hostile respawn days (not SaveGame) and, while IncreaseSpawnCounts is on, grows or trims spawner slot arrays (SaveGame).")},
        {TEXT("SpawnPerTier"), true, TEXT("structural"), TEXT("Spawn multiplier input, per unlocked milestone tier. Same writes as SpawnBase.")},
        {TEXT("SpawnMax"), true, TEXT("structural"), TEXT("Spawn multiplier cap. Same writes as SpawnBase.")},
        {TEXT("IncreaseSpawnCounts"), true, TEXT("structural"), TEXT("Gates growth and trim of HOSTILE spawner slot arrays (SaveGame). Off leaves grown hostile arrays at their current size; passive arrays are trimmed toward their recorded size regardless.")},
        {TEXT("KeepCreaturesRespawning"), true, TEXT("structural"), TEXT("Pins the near-base flag and re-arms killed slots (both SaveGame); widens the spawn radius of spawners starved for 3 consecutive sweeps, up to x3. Off restores radii and re-traces near-base on loaded spawners.")},
        {TEXT("VerboseRespawnLogging"), false, TEXT("none"), TEXT("Log output only. Forced quiet while Enabled is off.")},
    };

    FString Out = McpHead();
    Out += TEXT(",\"authority\":\"server\",\"logCategories\":[\"LogHostileScaling\"]");
    Out += TEXT(",\"snapshotArgs\":{\"section\":\"absent for a summary, or all | spawners | nearest\",");
    Out += FString::Printf(TEXT("\"count\":\"integer row count for spawners, all and nearest, default %d, clamped to 1..%d\","), McpDefaultRows, McpMaxRows);
    Out += TEXT("\"offset\":\"integer row offset for spawners, all and nearest; spawners and all are sorted by path key, nearest by distance at call time\"}");
    Out += TEXT(",\"config\":{\"kind\":\"sml-config\",\"id\":\"HostileScaling\",\"keys\":{");
    bool bFirst = true;
    for (const FKeyRow& Row : Keys)
    {
        Out += bFirst ? TEXT("") : TEXT(",");
        bFirst = false;
        Out += JStr(Row.Key) + FString::Printf(TEXT(":{\"live\":\"tick\",\"liveLatencySeconds\":%d,\"persists\":\"cfg\",\"touchesSave\":%s,\"saveEffect\":"),
                McpRefreshIntervalSeconds, JBool(Row.bTouchesSave))
            + JStr(Row.SaveEffect) + TEXT(",\"note\":") + JStr(Row.Note) + TEXT("}");
    }
    Out += TEXT("}},\"actions\":[]");

    // Row-field meanings (contract R4.3): the mod's own bookkeeping and the spawner's fields, read at call time.
    static const TCHAR* const Help[][2] = {
        {TEXT("rows"), TEXT("One row per spawner actor present in the world at call time (loaded spawners only); loadedTotal counts the same set.")},
        {TEXT("key"), TEXT("The spawner's actor path name; also the key of the mod's saved per-spawner records.")},
        {TEXT("creatureClass"), TEXT("Name of the spawner's creature class; null when the spawner has none.")},
        {TEXT("hostile"), TEXT("True when the class default object is not passive, false when it is; null when the class is missing or its default object has not been created.")},
        {TEXT("slots"), TEXT("Entries in the spawner's spawn data array now.")},
        {TEXT("originalSlots"), TEXT("Slot count the mod recorded, before any scaling, the first time its sweep reached this spawner with IncreaseSpawnCounts on (earlier versions also recorded passive spawners); null when the mod holds no record.")},
        {TEXT("waitingSlots"), TEXT("Slots the game flags as waiting for a spawn location now.")},
        {TEXT("respawnDays"), TEXT("The spawner's respawn time in in-game days now; -1 is the game's never-respawn value.")},
        {TEXT("originalRespawnDays"), TEXT("Respawn days the mod recorded the first time it saw this spawner; null when it holds no record.")},
        {TEXT("nearBase"), TEXT("The spawner's cached near-base flag now.")},
        {TEXT("starvationStreak"), TEXT("Consecutive sweeps this session, while the keeper is on, that found a waiting slot on this spawner; resets to 0 on reaching 3 and is removed when no slot waits; null when the mod holds no entry.")},
        {TEXT("spawnRadius"), TEXT("The spawner's spawn radius now, in world units.")},
        {TEXT("originalSpawnRadius"), TEXT("Radius the mod recorded before its first widening of this spawner; null when it never widened it.")},
        {TEXT("widenedThisSession"), TEXT("True when the mod widened this spawner's radius during this session.")},
        {TEXT("location"), TEXT("The spawner actor's location in world units as the game reports it.")},
        {TEXT("distanceCm"), TEXT("Distance from the first player pawn, nearest section only.")},
    };
    Out += TEXT(",\"help\":{\"rowFields\":{");
    bFirst = true;
    for (const auto& H : Help)
    {
        Out += (bFirst ? TEXT("") : TEXT(",")) + JStr(H[0]) + TEXT(":") + JStr(H[1]);
        bFirst = false;
    }
    Out += TEXT("}}}");
    return Out;
}

FString AHostileScalingSubsystem::McpSpawnerRow(const AFGCreatureSpawner* Spawner, double DistanceCm) const
{
    const FString Key = Spawner->GetPathName();
    const int32 Class = ClassifySpawner(Spawner);
    int32 WaitingSlots = 0;
    for (const FSpawnData& Slot : Spawner->mSpawnData)   // friend grant (AccessTransformers.ini)
    {
        WaitingSlots += Slot.WaitingForSpawnLocation ? 1 : 0;
    }
    const int32* OrigCount = OriginalSpawnCounts.Find(Key);
    const int32* OrigDays = OriginalRespawnDays.Find(Key);
    const float* OrigRadius = OriginalSpawnRadii.Find(Key);
    const int32* Streak = StarvationStreaks.Find(Key);

    FString Row = TEXT("{\"key\":") + JStr(Key);
    Row += TEXT(",\"creatureClass\":") + (Class == 2 ? FString(TEXT("null")) : JStr(GetNameSafe(Spawner->GetCreatureClass().Get())));
    Row += TEXT(",\"hostile\":") + FString(Class == 0 ? TEXT("true") : (Class == 1 ? TEXT("false") : TEXT("null")));
    Row += FString::Printf(TEXT(",\"slots\":%d,\"originalSlots\":%s,\"waitingSlots\":%d"), Spawner->mSpawnData.Num(),
        OrigCount ? *FString::FromInt(*OrigCount) : TEXT("null"), WaitingSlots);
    Row += FString::Printf(TEXT(",\"respawnDays\":%d,\"originalRespawnDays\":%s"), Spawner->mRespawnTimeIndays,
        OrigDays ? *FString::FromInt(*OrigDays) : TEXT("null"));
    Row += FString::Printf(TEXT(",\"nearBase\":%s,\"starvationStreak\":%s"), JBool(Spawner->IsNearBase()),
        Streak ? *FString::FromInt(*Streak) : TEXT("null"));
    Row += TEXT(",\"spawnRadius\":") + JNum(Spawner->GetSpawnRadius());
    Row += TEXT(",\"originalSpawnRadius\":") + (OrigRadius ? JNum(*OrigRadius) : FString(TEXT("null")));
    Row += FString::Printf(TEXT(",\"widenedThisSession\":%s"), JBool(WidenedSpawnerPaths.Contains(Key)));
    const FVector Loc = Spawner->GetActorLocation();
    Row += TEXT(",\"location\":{\"x\":") + JNum(Loc.X) + TEXT(",\"y\":") + JNum(Loc.Y) + TEXT(",\"z\":") + JNum(Loc.Z) + TEXT("}");
    if (DistanceCm >= 0.0)
    {
        Row += TEXT(",\"distanceCm\":") + JNum(DistanceCm);
    }
    Row += TEXT("}");
    return Row;
}

FString AHostileScalingSubsystem::McpSnapshot(const FString& ArgsJson)
{
    // ---- Arguments. --------------------------------------------------------
    FString Section;
    int32 Count = McpDefaultRows;
    int32 Offset = 0;
    const FString Trimmed = ArgsJson.TrimStartAndEnd();
    if (Trimmed.Len() > McpMaxArgsChars)
    {
        return McpError(TEXT("bad-args"), FString::Printf(TEXT("args longer than %d chars"), McpMaxArgsChars));
    }
    if (!Trimmed.IsEmpty())
    {
        TMap<FString, FString> StrArgs;
        TMap<FString, int32> IntArgs;
        FString ParseError;
        if (!ParseFlatArgs(Trimmed, StrArgs, IntArgs, ParseError))
        {
            return McpError(TEXT("bad-args"), ParseError);
        }
        for (const TPair<FString, FString>& P : StrArgs)
        {
            if (P.Key != TEXT("section")) { return McpError(TEXT("bad-args"), TEXT("unknown or non-integer-expected key: ") + ClipEcho(P.Key)); }
            Section = P.Value;
        }
        for (const TPair<FString, int32>& P : IntArgs)
        {
            if (P.Key == TEXT("count")) { Count = P.Value; }
            else if (P.Key == TEXT("offset")) { Offset = P.Value; }
            else { return McpError(TEXT("bad-args"), TEXT("unknown or non-string-expected key: ") + ClipEcho(P.Key)); }
        }
        if (!Section.IsEmpty() && Section != TEXT("all") && Section != TEXT("spawners") && Section != TEXT("nearest"))
        {
            return McpError(TEXT("bad-args"), TEXT("section must be all, spawners or nearest; got ") + ClipEcho(Section));
        }
        if (Offset < 0)
        {
            return McpError(TEXT("bad-args"), TEXT("offset must be 0 or more"));
        }
    }
    const int32 RequestedCount = Count;
    Count = FMath::Clamp(Count, 1, McpMaxRows);

    UWorld* World = GetWorld();
    if (!World || !GEngine)
    {
        return McpError(TEXT("no-world"), TEXT("subsystem has no world"));
    }

    FString Out = McpHead();
    Out += TEXT(",\"section\":") + (Section.IsEmpty() ? FString(TEXT("\"summary\"")) : JStr(Section));
    Out += FString::Printf(TEXT(",\"netMode\":%d,\"hasAuthority\":%s,\"hasBegunPlay\":%s"),
        static_cast<int32>(World->GetNetMode()), JBool(HasAuthority()), JBool(HasActorBegunPlay()));

    // ---- Summary (absent section, or all). ---------------------------------
    if (Section.IsEmpty() || Section == TEXT("all"))
    {
        UGameInstance* GameInstance = World->GetGameInstance();
        const bool bConfigManager = GameInstance && GameInstance->GetSubsystem<UConfigManager>();
        // GetActiveConfig dereferences game instance and config manager unchecked; both checked above.
        const FHostileScalingConfigStruct C = bConfigManager ? FHostileScalingConfigStruct::GetActiveConfig(World) : FHostileScalingConfigStruct{};
        // The mod itself only walks to the schematic manager from the timer (>= 2 s
        // after BeginPlay); this function can be called earlier, so gate the walk
        // into closed code on a public predicate.
        const bool bGameState = World->GetGameState() != nullptr;
        const bool bSchematicManager = bGameState && AFGSchematicManager::Get(World) != nullptr;
        const int32 Tier = bSchematicManager ? GetHighestUnlockedTier() : 0;

        Out += FString::Printf(TEXT(",\"configManagerPresent\":%s,\"gameState\":%s,\"schematicManager\":%s,\"highestUnlockedTier\":%s"),
            JBool(bConfigManager), JBool(bGameState), JBool(bSchematicManager),
            bSchematicManager ? *FString::FromInt(Tier) : TEXT("null"));
        if (bConfigManager && bSchematicManager)
        {
        Out += TEXT(",\"computedMultipliers\":{\"health\":") + JNum(ComputeMultiplier(C.HealthBase, C.HealthPerTier, C.HealthMax, Tier))
            + TEXT(",\"damage\":") + JNum(ComputeMultiplier(C.DamageBase, C.DamagePerTier, C.DamageMax, Tier))
            + TEXT(",\"spawn\":") + JNum(ComputeMultiplier(C.SpawnBase, C.SpawnPerTier, C.SpawnMax, Tier)) + TEXT("}");
        }
        else
        {
            Out += TEXT(",\"computedMultipliers\":null");
        }
        Out += TEXT(",\"publishedGlobals\":{\"healthMultiplier\":") + JNum(HostileScaling::GCreatureHealthMultiplier)
            + TEXT(",\"damageMultiplier\":") + JNum(HostileScaling::GCreatureDamageMultiplier)
            + FString::Printf(TEXT(",\"scaleDamageToBuildings\":%s,\"keepCreaturesRespawning\":%s,\"verboseRespawnLogging\":%s,\"widenedSpawnRadiusCount\":%d}"),
                JBool(HostileScaling::GScaleDamageToBuildings), JBool(HostileScaling::GKeepCreaturesRespawning),
                JBool(HostileScaling::GVerboseRespawnLogging), HostileScaling::GWidenedSpawnRadiusCount);
        if (bConfigManager)
        {
        Out += FString::Printf(TEXT(",\"config\":{\"Enabled\":%s,\"HealthBase\":%s,\"HealthPerTier\":%s,\"HealthMax\":%s"),
            JBool(C.Enabled), *JNum(C.HealthBase), *JNum(C.HealthPerTier), *JNum(C.HealthMax));
        Out += FString::Printf(TEXT(",\"DamageBase\":%s,\"DamagePerTier\":%s,\"DamageMax\":%s,\"SpawnBase\":%s,\"SpawnPerTier\":%s,\"SpawnMax\":%s"),
            *JNum(C.DamageBase), *JNum(C.DamagePerTier), *JNum(C.DamageMax), *JNum(C.SpawnBase), *JNum(C.SpawnPerTier), *JNum(C.SpawnMax));
        Out += FString::Printf(TEXT(",\"ScaleDamageToBuildings\":%s,\"IncreaseSpawnCounts\":%s,\"KeepCreaturesRespawning\":%s,\"VerboseRespawnLogging\":%s}"),
            JBool(C.ScaleDamageToBuildings), JBool(C.IncreaseSpawnCounts), JBool(C.KeepCreaturesRespawning), JBool(C.VerboseRespawnLogging));
        }
        else
        {
            Out += TEXT(",\"config\":null");
        }
        Out += FString::Printf(TEXT(",\"keeper\":{\"configActive\":%s,\"activeAtLastSweep\":%s}"),
            bConfigManager ? JBool(C.Enabled && C.KeepCreaturesRespawning) : TEXT("null"), JBool(bRespawnKeeperWasActive));

        // noCdo is its own bucket; the mod's sweep would create the CDO and classify it
        int32 Hostile = 0, Passive = 0, NullClass = 0, NoCdo = 0;
        for (TActorIterator<AFGCreatureSpawner> It(World); It; ++It)
        {
            const AFGCreatureSpawner* Spawner = *It;
            if (!IsValid(Spawner)) { continue; }
            const int32 Class = ClassifySpawner(Spawner);
            Hostile += Class == 0 ? 1 : 0;
            Passive += Class == 1 ? 1 : 0;
            NullClass += Class == 2 ? 1 : 0;
            NoCdo += Class == 3 ? 1 : 0;
        }
        int32 PositiveStreaks = 0;
        for (const TPair<FString, int32>& P : StarvationStreaks)
        {
            PositiveStreaks += P.Value > 0 ? 1 : 0;
        }
        Out += FString::Printf(TEXT(",\"census\":{\"hostile\":%d,\"passive\":%d,\"nullClass\":%d,\"classWithoutCdo\":%d"), Hostile, Passive, NullClass, NoCdo);
        Out += FString::Printf(TEXT(",\"highWaterHostile\":%d,\"highWaterPassive\":%d,\"highWaterNullClass\":%d}"),
            CensusHighWaterHostile, CensusHighWaterPassive, CensusHighWaterNullClass);
        Out += FString::Printf(TEXT(",\"sessionSets\":{\"widened\":%d,\"starvationEntries\":%d,\"starvationPositive\":%d,\"cappedStarved\":%d,\"passiveReverted\":%d}"),
            WidenedSpawnerPaths.Num(), StarvationStreaks.Num(), PositiveStreaks, CappedStarvedLoggedPaths.Num(), RevertedPassiveSpawnerPaths.Num());
        Out += FString::Printf(TEXT(",\"saveMaps\":{\"originalRespawnDays\":%d,\"originalSpawnCounts\":%d,\"originalSpawnRadii\":%d}"),
            OriginalRespawnDays.Num(), OriginalSpawnCounts.Num(), OriginalSpawnRadii.Num());
    }

    // ---- Rows (spawners, all, nearest). -----------------------------------
    int32 Rows = 0;
    bool bTruncated = false;
    if (!Section.IsEmpty())
    {
        const bool bNearest = Section == TEXT("nearest");
        FVector PlayerLocation = FVector::ZeroVector;
        if (bNearest)
        {
            const APlayerController* PC = World->GetFirstPlayerController();
            const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
            if (!IsValid(Pawn))
            {
                return McpError(TEXT("no-player"), TEXT("no first player controller with a pawn"));
            }
            PlayerLocation = Pawn->GetActorLocation();
            Out += TEXT(",\"playerLocation\":[") + JNum(PlayerLocation.X) + TEXT(",") + JNum(PlayerLocation.Y) + TEXT(",") + JNum(PlayerLocation.Z) + TEXT("]");
        }

        struct FEntry { FString Key; double DistSq; const AFGCreatureSpawner* Spawner; };
        TArray<FEntry> Entries;
        for (TActorIterator<AFGCreatureSpawner> It(World); It; ++It)
        {
            const AFGCreatureSpawner* Spawner = *It;
            if (!IsValid(Spawner)) { continue; }
            Entries.Add(FEntry{bNearest ? FString() : Spawner->GetPathName(),
                bNearest ? FVector::DistSquared(PlayerLocation, Spawner->GetActorLocation()) : 0.0, Spawner});
        }
        if (bNearest)
        {
            Entries.Sort([](const FEntry& A, const FEntry& B) { return A.DistSq < B.DistSq; });
        }
        else
        {
            Entries.Sort([](const FEntry& A, const FEntry& B) { return A.Key < B.Key; });
        }

        const int32 Start = Offset;   // every row section pages, nearest included (sorted by distance)
        Out += FString::Printf(TEXT(",\"loadedTotal\":%d,\"offset\":%d,\"countRequested\":%d,\"countUsed\":%d,\"rows\":["),
            Entries.Num(), Start, RequestedCount, Count);
        for (int32 Idx = Start; Idx < Entries.Num() && Rows < Count; ++Idx)
        {
            const FString Row = McpSpawnerRow(Entries[Idx].Spawner, bNearest ? FMath::Sqrt(Entries[Idx].DistSq) : -1.0);
            if (Out.Len() + Row.Len() > McpRowBudgetChars)
            {
                bTruncated = true;
                break;
            }
            Out += (Rows > 0 ? TEXT(",") : TEXT("")) + Row;
            ++Rows;
        }
        // truncated = stopped by the size budget; hasMore = rows remain past this page.
        const bool bHasMore = Start + Rows < Entries.Num();
        Out += FString::Printf(TEXT("],\"returned\":%d,\"truncated\":%s,\"hasMore\":%s"), Rows, JBool(bTruncated), JBool(bHasMore));
    }
    Out += TEXT("}");

    UE_LOG(LogHostileScaling, Verbose, TEXT("[MCP] McpSnapshot section %s rows %d truncated %d chars %d"),
        Section.IsEmpty() ? TEXT("summary") : *Section, Rows, bTruncated ? 1 : 0, Out.Len());
    return Out;
}
