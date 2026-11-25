#include "engine/props_engine.h"

#include "core/objects.h"

#include <cstdio>

namespace ut {
    actor::Offsets offsets_actor;
    pawn::Offsets offsets_pawn;
    playerpawn::Offsets offsets_playerpawn;
    levelinfo::Offsets offsets_levelinfo;
    zoneinfo::Offsets offsets_zoneinfo;
    skyzoneinfo::Offsets offsets_skyzoneinfo;
    warpzoneinfo::Offsets offsets_warpzoneinfo;
    gameinfo::Offsets offsets_gameinfo;
    brush::Offsets offsets_brush;
    mover::Offsets offsets_mover;
    navigationpoint::Offsets offsets_navigationpoint;
    inventory::Offsets offsets_inventory;
    weapon::Offsets offsets_weapon;
    projectile::Offsets offsets_projectile;
    decoration::Offsets offsets_decoration;
    canvas::Offsets offsets_canvas;
    console::Offsets offsets_console;
    hud::Offsets offsets_hud;
    bitmap::Offsets offsets_bitmap;
    texture::Offsets offsets_texture;
    player::Offsets offsets_player;
    light::Offsets offsets_light;
    info::Offsets offsets_info;
    playerreplicationinfo::Offsets offsets_playerreplicationinfo;
    decal::Offsets offsets_decal;
    teleporter::Offsets offsets_teleporter;
    scriptedtexture::Offsets offsets_scriptedtexture;
    carcass::Offsets offsets_carcass;
    ammo::Offsets offsets_ammo;
    pickup::Offsets offsets_pickup;
    pathnode::Offsets offsets_pathnode;
    trigger::Offsets offsets_trigger;
    effects::Offsets offsets_effects;
    spawnnotify::Offsets offsets_spawnnotify;
    keypoint::Offsets offsets_keypoint;
    mesh::Offsets offsets_mesh;

    namespace {
        size_t propertyOffset(UClass* cls, const char* name)
        {
            UProperty* property = cls->findProperty(Name(name));
            if (!property) {
                return MissingProperty;
            }
            return property->offset;
        }
    }

    void bindProps_engine()
    {
        {
            UClass* cls = objects::findClass("Engine.Actor");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Actor\n");
            } else {
                offsets_actor.bStatic = propertyOffset(cls, "bStatic");
                offsets_actor.bHidden = propertyOffset(cls, "bHidden");
                offsets_actor.bNoDelete = propertyOffset(cls, "bNoDelete");
                offsets_actor.bAnimFinished = propertyOffset(cls, "bAnimFinished");
                offsets_actor.bAnimLoop = propertyOffset(cls, "bAnimLoop");
                offsets_actor.bAnimNotify = propertyOffset(cls, "bAnimNotify");
                offsets_actor.bAnimByOwner = propertyOffset(cls, "bAnimByOwner");
                offsets_actor.bDeleteMe = propertyOffset(cls, "bDeleteMe");
                offsets_actor.bAssimilated = propertyOffset(cls, "bAssimilated");
                offsets_actor.bTicked = propertyOffset(cls, "bTicked");
                offsets_actor.bLightChanged = propertyOffset(cls, "bLightChanged");
                offsets_actor.bDynamicLight = propertyOffset(cls, "bDynamicLight");
                offsets_actor.bTimerLoop = propertyOffset(cls, "bTimerLoop");
                offsets_actor.bCanTeleport = propertyOffset(cls, "bCanTeleport");
                offsets_actor.bIsSecretGoal = propertyOffset(cls, "bIsSecretGoal");
                offsets_actor.bIsKillGoal = propertyOffset(cls, "bIsKillGoal");
                offsets_actor.bIsItemGoal = propertyOffset(cls, "bIsItemGoal");
                offsets_actor.bCollideWhenPlacing = propertyOffset(cls, "bCollideWhenPlacing");
                offsets_actor.bTravel = propertyOffset(cls, "bTravel");
                offsets_actor.bMovable = propertyOffset(cls, "bMovable");
                offsets_actor.bHighDetail = propertyOffset(cls, "bHighDetail");
                offsets_actor.bStasis = propertyOffset(cls, "bStasis");
                offsets_actor.bForceStasis = propertyOffset(cls, "bForceStasis");
                offsets_actor.bIsPawn = propertyOffset(cls, "bIsPawn");
                offsets_actor.bNetTemporary = propertyOffset(cls, "bNetTemporary");
                offsets_actor.bNetOptional = propertyOffset(cls, "bNetOptional");
                offsets_actor.bReplicateInstigator = propertyOffset(cls, "bReplicateInstigator");
                offsets_actor.bTrailerSameRotation = propertyOffset(cls, "bTrailerSameRotation");
                offsets_actor.bTrailerPrePivot = propertyOffset(cls, "bTrailerPrePivot");
                offsets_actor.bClientAnim = propertyOffset(cls, "bClientAnim");
                offsets_actor.bSimFall = propertyOffset(cls, "bSimFall");
                offsets_actor.Physics = propertyOffset(cls, "Physics");
                offsets_actor.Role = propertyOffset(cls, "Role");
                offsets_actor.RemoteRole = propertyOffset(cls, "RemoteRole");
                offsets_actor.Owner = propertyOffset(cls, "Owner");
                offsets_actor.InitialState = propertyOffset(cls, "InitialState");
                offsets_actor.Group = propertyOffset(cls, "Group");
                offsets_actor.TimerRate = propertyOffset(cls, "TimerRate");
                offsets_actor.TimerCounter = propertyOffset(cls, "TimerCounter");
                offsets_actor.LifeSpan = propertyOffset(cls, "LifeSpan");
                offsets_actor.AnimSequence = propertyOffset(cls, "AnimSequence");
                offsets_actor.AnimFrame = propertyOffset(cls, "AnimFrame");
                offsets_actor.AnimRate = propertyOffset(cls, "AnimRate");
                offsets_actor.TweenRate = propertyOffset(cls, "TweenRate");
                offsets_actor.LODBias = propertyOffset(cls, "LODBias");
                offsets_actor.Level = propertyOffset(cls, "Level");
                offsets_actor.XLevel = propertyOffset(cls, "XLevel");
                offsets_actor.Tag = propertyOffset(cls, "Tag");
                offsets_actor.Event = propertyOffset(cls, "Event");
                offsets_actor.Target = propertyOffset(cls, "Target");
                offsets_actor.Instigator = propertyOffset(cls, "Instigator");
                offsets_actor.Inventory = propertyOffset(cls, "Inventory");
                offsets_actor.Base = propertyOffset(cls, "Base");
                offsets_actor.Region = propertyOffset(cls, "Region");
                offsets_actor.AttachTag = propertyOffset(cls, "AttachTag");
                offsets_actor.StandingCount = propertyOffset(cls, "StandingCount");
                offsets_actor.MiscNumber = propertyOffset(cls, "MiscNumber");
                offsets_actor.LatentByte = propertyOffset(cls, "LatentByte");
                offsets_actor.LatentInt = propertyOffset(cls, "LatentInt");
                offsets_actor.LatentFloat = propertyOffset(cls, "LatentFloat");
                offsets_actor.LatentActor = propertyOffset(cls, "LatentActor");
                offsets_actor.Touching = propertyOffset(cls, "Touching");
                offsets_actor.Deleted = propertyOffset(cls, "Deleted");
                offsets_actor.CollisionTag = propertyOffset(cls, "CollisionTag");
                offsets_actor.LightingTag = propertyOffset(cls, "LightingTag");
                offsets_actor.NetTag = propertyOffset(cls, "NetTag");
                offsets_actor.OtherTag = propertyOffset(cls, "OtherTag");
                offsets_actor.ExtraTag = propertyOffset(cls, "ExtraTag");
                offsets_actor.SpecialTag = propertyOffset(cls, "SpecialTag");
                offsets_actor.Location = propertyOffset(cls, "Location");
                offsets_actor.Rotation = propertyOffset(cls, "Rotation");
                offsets_actor.OldLocation = propertyOffset(cls, "OldLocation");
                offsets_actor.ColLocation = propertyOffset(cls, "ColLocation");
                offsets_actor.Velocity = propertyOffset(cls, "Velocity");
                offsets_actor.Acceleration = propertyOffset(cls, "Acceleration");
                offsets_actor.bHiddenEd = propertyOffset(cls, "bHiddenEd");
                offsets_actor.bDirectional = propertyOffset(cls, "bDirectional");
                offsets_actor.bSelected = propertyOffset(cls, "bSelected");
                offsets_actor.bMemorized = propertyOffset(cls, "bMemorized");
                offsets_actor.bHighlighted = propertyOffset(cls, "bHighlighted");
                offsets_actor.bEdLocked = propertyOffset(cls, "bEdLocked");
                offsets_actor.bEdShouldSnap = propertyOffset(cls, "bEdShouldSnap");
                offsets_actor.bEdSnap = propertyOffset(cls, "bEdSnap");
                offsets_actor.bTempEditor = propertyOffset(cls, "bTempEditor");
                offsets_actor.bDifficulty0 = propertyOffset(cls, "bDifficulty0");
                offsets_actor.bDifficulty1 = propertyOffset(cls, "bDifficulty1");
                offsets_actor.bDifficulty2 = propertyOffset(cls, "bDifficulty2");
                offsets_actor.bDifficulty3 = propertyOffset(cls, "bDifficulty3");
                offsets_actor.bSinglePlayer = propertyOffset(cls, "bSinglePlayer");
                offsets_actor.bNet = propertyOffset(cls, "bNet");
                offsets_actor.bNetSpecial = propertyOffset(cls, "bNetSpecial");
                offsets_actor.OddsOfAppearing = propertyOffset(cls, "OddsOfAppearing");
                offsets_actor.DrawType = propertyOffset(cls, "DrawType");
                offsets_actor.Style = propertyOffset(cls, "Style");
                offsets_actor.Sprite = propertyOffset(cls, "Sprite");
                offsets_actor.Texture = propertyOffset(cls, "Texture");
                offsets_actor.Skin = propertyOffset(cls, "Skin");
                offsets_actor.Mesh = propertyOffset(cls, "Mesh");
                offsets_actor.Brush = propertyOffset(cls, "Brush");
                offsets_actor.DrawScale = propertyOffset(cls, "DrawScale");
                offsets_actor.PrePivot = propertyOffset(cls, "PrePivot");
                offsets_actor.ScaleGlow = propertyOffset(cls, "ScaleGlow");
                offsets_actor.AmbientGlow = propertyOffset(cls, "AmbientGlow");
                offsets_actor.Fatness = propertyOffset(cls, "Fatness");
                offsets_actor.bUnlit = propertyOffset(cls, "bUnlit");
                offsets_actor.bNoSmooth = propertyOffset(cls, "bNoSmooth");
                offsets_actor.bParticles = propertyOffset(cls, "bParticles");
                offsets_actor.bRandomFrame = propertyOffset(cls, "bRandomFrame");
                offsets_actor.bMeshEnviroMap = propertyOffset(cls, "bMeshEnviroMap");
                offsets_actor.bMeshCurvy = propertyOffset(cls, "bMeshCurvy");
                offsets_actor.VisibilityRadius = propertyOffset(cls, "VisibilityRadius");
                offsets_actor.VisibilityHeight = propertyOffset(cls, "VisibilityHeight");
                offsets_actor.bShadowCast = propertyOffset(cls, "bShadowCast");
                offsets_actor.bOwnerNoSee = propertyOffset(cls, "bOwnerNoSee");
                offsets_actor.bOnlyOwnerSee = propertyOffset(cls, "bOnlyOwnerSee");
                offsets_actor.bIsMover = propertyOffset(cls, "bIsMover");
                offsets_actor.bAlwaysRelevant = propertyOffset(cls, "bAlwaysRelevant");
                offsets_actor.bAlwaysTick = propertyOffset(cls, "bAlwaysTick");
                offsets_actor.bHurtEntry = propertyOffset(cls, "bHurtEntry");
                offsets_actor.bGameRelevant = propertyOffset(cls, "bGameRelevant");
                offsets_actor.bCarriedItem = propertyOffset(cls, "bCarriedItem");
                offsets_actor.bForcePhysicsUpdate = propertyOffset(cls, "bForcePhysicsUpdate");
                offsets_actor.MultiSkins = propertyOffset(cls, "MultiSkins");
                offsets_actor.SoundRadius = propertyOffset(cls, "SoundRadius");
                offsets_actor.SoundVolume = propertyOffset(cls, "SoundVolume");
                offsets_actor.SoundPitch = propertyOffset(cls, "SoundPitch");
                offsets_actor.AmbientSound = propertyOffset(cls, "AmbientSound");
                offsets_actor.TransientSoundVolume = propertyOffset(cls, "TransientSoundVolume");
                offsets_actor.TransientSoundRadius = propertyOffset(cls, "TransientSoundRadius");
                offsets_actor.CollisionRadius = propertyOffset(cls, "CollisionRadius");
                offsets_actor.CollisionHeight = propertyOffset(cls, "CollisionHeight");
                offsets_actor.bCollideActors = propertyOffset(cls, "bCollideActors");
                offsets_actor.bCollideWorld = propertyOffset(cls, "bCollideWorld");
                offsets_actor.bBlockActors = propertyOffset(cls, "bBlockActors");
                offsets_actor.bBlockPlayers = propertyOffset(cls, "bBlockPlayers");
                offsets_actor.bProjTarget = propertyOffset(cls, "bProjTarget");
                offsets_actor.LightType = propertyOffset(cls, "LightType");
                offsets_actor.LightEffect = propertyOffset(cls, "LightEffect");
                offsets_actor.LightBrightness = propertyOffset(cls, "LightBrightness");
                offsets_actor.LightHue = propertyOffset(cls, "LightHue");
                offsets_actor.LightSaturation = propertyOffset(cls, "LightSaturation");
                offsets_actor.LightRadius = propertyOffset(cls, "LightRadius");
                offsets_actor.LightPeriod = propertyOffset(cls, "LightPeriod");
                offsets_actor.LightPhase = propertyOffset(cls, "LightPhase");
                offsets_actor.LightCone = propertyOffset(cls, "LightCone");
                offsets_actor.VolumeBrightness = propertyOffset(cls, "VolumeBrightness");
                offsets_actor.VolumeRadius = propertyOffset(cls, "VolumeRadius");
                offsets_actor.VolumeFog = propertyOffset(cls, "VolumeFog");
                offsets_actor.bSpecialLit = propertyOffset(cls, "bSpecialLit");
                offsets_actor.bActorShadows = propertyOffset(cls, "bActorShadows");
                offsets_actor.bCorona = propertyOffset(cls, "bCorona");
                offsets_actor.bLensFlare = propertyOffset(cls, "bLensFlare");
                offsets_actor.bBounce = propertyOffset(cls, "bBounce");
                offsets_actor.bFixedRotationDir = propertyOffset(cls, "bFixedRotationDir");
                offsets_actor.bRotateToDesired = propertyOffset(cls, "bRotateToDesired");
                offsets_actor.bInterpolating = propertyOffset(cls, "bInterpolating");
                offsets_actor.bJustTeleported = propertyOffset(cls, "bJustTeleported");
                offsets_actor.DodgeDir = propertyOffset(cls, "DodgeDir");
                offsets_actor.Mass = propertyOffset(cls, "Mass");
                offsets_actor.Buoyancy = propertyOffset(cls, "Buoyancy");
                offsets_actor.RotationRate = propertyOffset(cls, "RotationRate");
                offsets_actor.DesiredRotation = propertyOffset(cls, "DesiredRotation");
                offsets_actor.PhysAlpha = propertyOffset(cls, "PhysAlpha");
                offsets_actor.PhysRate = propertyOffset(cls, "PhysRate");
                offsets_actor.PendingTouch = propertyOffset(cls, "PendingTouch");
                offsets_actor.AnimLast = propertyOffset(cls, "AnimLast");
                offsets_actor.AnimMinRate = propertyOffset(cls, "AnimMinRate");
                offsets_actor.OldAnimRate = propertyOffset(cls, "OldAnimRate");
                offsets_actor.SimAnim = propertyOffset(cls, "SimAnim");
                offsets_actor.NetPriority = propertyOffset(cls, "NetPriority");
                offsets_actor.NetUpdateFrequency = propertyOffset(cls, "NetUpdateFrequency");
                offsets_actor.bNetInitial = propertyOffset(cls, "bNetInitial");
                offsets_actor.bNetOwner = propertyOffset(cls, "bNetOwner");
                offsets_actor.bNetRelevant = propertyOffset(cls, "bNetRelevant");
                offsets_actor.bNetSee = propertyOffset(cls, "bNetSee");
                offsets_actor.bNetHear = propertyOffset(cls, "bNetHear");
                offsets_actor.bNetFeel = propertyOffset(cls, "bNetFeel");
                offsets_actor.bSimulatedPawn = propertyOffset(cls, "bSimulatedPawn");
                offsets_actor.bDemoRecording = propertyOffset(cls, "bDemoRecording");
                offsets_actor.bClientDemoRecording = propertyOffset(cls, "bClientDemoRecording");
                offsets_actor.bClientDemoNetFunc = propertyOffset(cls, "bClientDemoNetFunc");
                offsets_actor.RenderIteratorClass = propertyOffset(cls, "RenderIteratorClass");
                offsets_actor.RenderInterface = propertyOffset(cls, "RenderInterface");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Pawn");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Pawn\n");
            } else {
                offsets_pawn.bBehindView = propertyOffset(cls, "bBehindView");
                offsets_pawn.bIsPlayer = propertyOffset(cls, "bIsPlayer");
                offsets_pawn.bJustLanded = propertyOffset(cls, "bJustLanded");
                offsets_pawn.bUpAndOut = propertyOffset(cls, "bUpAndOut");
                offsets_pawn.bIsWalking = propertyOffset(cls, "bIsWalking");
                offsets_pawn.bHitSlopedWall = propertyOffset(cls, "bHitSlopedWall");
                offsets_pawn.bNeverSwitchOnPickup = propertyOffset(cls, "bNeverSwitchOnPickup");
                offsets_pawn.bWarping = propertyOffset(cls, "bWarping");
                offsets_pawn.bUpdatingDisplay = propertyOffset(cls, "bUpdatingDisplay");
                offsets_pawn.bCanStrafe = propertyOffset(cls, "bCanStrafe");
                offsets_pawn.bFixedStart = propertyOffset(cls, "bFixedStart");
                offsets_pawn.bReducedSpeed = propertyOffset(cls, "bReducedSpeed");
                offsets_pawn.bCanJump = propertyOffset(cls, "bCanJump");
                offsets_pawn.bCanWalk = propertyOffset(cls, "bCanWalk");
                offsets_pawn.bCanSwim = propertyOffset(cls, "bCanSwim");
                offsets_pawn.bCanFly = propertyOffset(cls, "bCanFly");
                offsets_pawn.bCanOpenDoors = propertyOffset(cls, "bCanOpenDoors");
                offsets_pawn.bCanDoSpecial = propertyOffset(cls, "bCanDoSpecial");
                offsets_pawn.bDrowning = propertyOffset(cls, "bDrowning");
                offsets_pawn.bLOSflag = propertyOffset(cls, "bLOSflag");
                offsets_pawn.bFromWall = propertyOffset(cls, "bFromWall");
                offsets_pawn.bHunting = propertyOffset(cls, "bHunting");
                offsets_pawn.bAvoidLedges = propertyOffset(cls, "bAvoidLedges");
                offsets_pawn.bStopAtLedges = propertyOffset(cls, "bStopAtLedges");
                offsets_pawn.bJumpOffPawn = propertyOffset(cls, "bJumpOffPawn");
                offsets_pawn.bShootSpecial = propertyOffset(cls, "bShootSpecial");
                offsets_pawn.bAutoActivate = propertyOffset(cls, "bAutoActivate");
                offsets_pawn.bIsHuman = propertyOffset(cls, "bIsHuman");
                offsets_pawn.bIsFemale = propertyOffset(cls, "bIsFemale");
                offsets_pawn.bIsMultiSkinned = propertyOffset(cls, "bIsMultiSkinned");
                offsets_pawn.bCountJumps = propertyOffset(cls, "bCountJumps");
                offsets_pawn.bAdvancedTactics = propertyOffset(cls, "bAdvancedTactics");
                offsets_pawn.bViewTarget = propertyOffset(cls, "bViewTarget");
                offsets_pawn.SightCounter = propertyOffset(cls, "SightCounter");
                offsets_pawn.PainTime = propertyOffset(cls, "PainTime");
                offsets_pawn.SpeechTime = propertyOffset(cls, "SpeechTime");
                offsets_pawn.AvgPhysicsTime = propertyOffset(cls, "AvgPhysicsTime");
                offsets_pawn.FootRegion = propertyOffset(cls, "FootRegion");
                offsets_pawn.HeadRegion = propertyOffset(cls, "HeadRegion");
                offsets_pawn.MoveTimer = propertyOffset(cls, "MoveTimer");
                offsets_pawn.MoveTarget = propertyOffset(cls, "MoveTarget");
                offsets_pawn.FaceTarget = propertyOffset(cls, "FaceTarget");
                offsets_pawn.Destination = propertyOffset(cls, "Destination");
                offsets_pawn.Focus = propertyOffset(cls, "Focus");
                offsets_pawn.DesiredSpeed = propertyOffset(cls, "DesiredSpeed");
                offsets_pawn.MaxDesiredSpeed = propertyOffset(cls, "MaxDesiredSpeed");
                offsets_pawn.MeleeRange = propertyOffset(cls, "MeleeRange");
                offsets_pawn.GroundSpeed = propertyOffset(cls, "GroundSpeed");
                offsets_pawn.WaterSpeed = propertyOffset(cls, "WaterSpeed");
                offsets_pawn.AirSpeed = propertyOffset(cls, "AirSpeed");
                offsets_pawn.AccelRate = propertyOffset(cls, "AccelRate");
                offsets_pawn.JumpZ = propertyOffset(cls, "JumpZ");
                offsets_pawn.MaxStepHeight = propertyOffset(cls, "MaxStepHeight");
                offsets_pawn.AirControl = propertyOffset(cls, "AirControl");
                offsets_pawn.MinHitWall = propertyOffset(cls, "MinHitWall");
                offsets_pawn.Visibility = propertyOffset(cls, "Visibility");
                offsets_pawn.Alertness = propertyOffset(cls, "Alertness");
                offsets_pawn.Stimulus = propertyOffset(cls, "Stimulus");
                offsets_pawn.SightRadius = propertyOffset(cls, "SightRadius");
                offsets_pawn.PeripheralVision = propertyOffset(cls, "PeripheralVision");
                offsets_pawn.HearingThreshold = propertyOffset(cls, "HearingThreshold");
                offsets_pawn.LastSeenPos = propertyOffset(cls, "LastSeenPos");
                offsets_pawn.LastSeeingPos = propertyOffset(cls, "LastSeeingPos");
                offsets_pawn.LastSeenTime = propertyOffset(cls, "LastSeenTime");
                offsets_pawn.Enemy = propertyOffset(cls, "Enemy");
                offsets_pawn.Weapon = propertyOffset(cls, "Weapon");
                offsets_pawn.PendingWeapon = propertyOffset(cls, "PendingWeapon");
                offsets_pawn.SelectedItem = propertyOffset(cls, "SelectedItem");
                offsets_pawn.ViewRotation = propertyOffset(cls, "ViewRotation");
                offsets_pawn.WalkBob = propertyOffset(cls, "WalkBob");
                offsets_pawn.BaseEyeHeight = propertyOffset(cls, "BaseEyeHeight");
                offsets_pawn.EyeHeight = propertyOffset(cls, "EyeHeight");
                offsets_pawn.Floor = propertyOffset(cls, "Floor");
                offsets_pawn.SplashTime = propertyOffset(cls, "SplashTime");
                offsets_pawn.OrthoZoom = propertyOffset(cls, "OrthoZoom");
                offsets_pawn.FovAngle = propertyOffset(cls, "FovAngle");
                offsets_pawn.DieCount = propertyOffset(cls, "DieCount");
                offsets_pawn.ItemCount = propertyOffset(cls, "ItemCount");
                offsets_pawn.KillCount = propertyOffset(cls, "KillCount");
                offsets_pawn.SecretCount = propertyOffset(cls, "SecretCount");
                offsets_pawn.Spree = propertyOffset(cls, "Spree");
                offsets_pawn.Health = propertyOffset(cls, "Health");
                offsets_pawn.SelectionMesh = propertyOffset(cls, "SelectionMesh");
                offsets_pawn.SpecialMesh = propertyOffset(cls, "SpecialMesh");
                offsets_pawn.ReducedDamageType = propertyOffset(cls, "ReducedDamageType");
                offsets_pawn.ReducedDamagePct = propertyOffset(cls, "ReducedDamagePct");
                offsets_pawn.DropWhenKilled = propertyOffset(cls, "DropWhenKilled");
                offsets_pawn.UnderWaterTime = propertyOffset(cls, "UnderWaterTime");
                offsets_pawn.AttitudeToPlayer = propertyOffset(cls, "AttitudeToPlayer");
                offsets_pawn.Intelligence = propertyOffset(cls, "Intelligence");
                offsets_pawn.Skill = propertyOffset(cls, "Skill");
                offsets_pawn.SpecialGoal = propertyOffset(cls, "SpecialGoal");
                offsets_pawn.SpecialPause = propertyOffset(cls, "SpecialPause");
                offsets_pawn.noise1spot = propertyOffset(cls, "noise1spot");
                offsets_pawn.noise1time = propertyOffset(cls, "noise1time");
                offsets_pawn.noise1other = propertyOffset(cls, "noise1other");
                offsets_pawn.noise1loudness = propertyOffset(cls, "noise1loudness");
                offsets_pawn.noise2spot = propertyOffset(cls, "noise2spot");
                offsets_pawn.noise2time = propertyOffset(cls, "noise2time");
                offsets_pawn.noise2other = propertyOffset(cls, "noise2other");
                offsets_pawn.noise2loudness = propertyOffset(cls, "noise2loudness");
                offsets_pawn.LastPainSound = propertyOffset(cls, "LastPainSound");
                offsets_pawn.nextPawn = propertyOffset(cls, "nextPawn");
                offsets_pawn.HitSound1 = propertyOffset(cls, "HitSound1");
                offsets_pawn.HitSound2 = propertyOffset(cls, "HitSound2");
                offsets_pawn.Land = propertyOffset(cls, "Land");
                offsets_pawn.Die = propertyOffset(cls, "Die");
                offsets_pawn.WaterStep = propertyOffset(cls, "WaterStep");
                offsets_pawn.bZoom = propertyOffset(cls, "bZoom");
                offsets_pawn.bRun = propertyOffset(cls, "bRun");
                offsets_pawn.bLook = propertyOffset(cls, "bLook");
                offsets_pawn.bDuck = propertyOffset(cls, "bDuck");
                offsets_pawn.bSnapLevel = propertyOffset(cls, "bSnapLevel");
                offsets_pawn.bStrafe = propertyOffset(cls, "bStrafe");
                offsets_pawn.bFire = propertyOffset(cls, "bFire");
                offsets_pawn.bAltFire = propertyOffset(cls, "bAltFire");
                offsets_pawn.bFreeLook = propertyOffset(cls, "bFreeLook");
                offsets_pawn.bExtra0 = propertyOffset(cls, "bExtra0");
                offsets_pawn.bExtra1 = propertyOffset(cls, "bExtra1");
                offsets_pawn.bExtra2 = propertyOffset(cls, "bExtra2");
                offsets_pawn.bExtra3 = propertyOffset(cls, "bExtra3");
                offsets_pawn.CombatStyle = propertyOffset(cls, "CombatStyle");
                offsets_pawn.home = propertyOffset(cls, "home");
                offsets_pawn.NextState = propertyOffset(cls, "NextState");
                offsets_pawn.NextLabel = propertyOffset(cls, "NextLabel");
                offsets_pawn.SoundDampening = propertyOffset(cls, "SoundDampening");
                offsets_pawn.DamageScaling = propertyOffset(cls, "DamageScaling");
                offsets_pawn.AlarmTag = propertyOffset(cls, "AlarmTag");
                offsets_pawn.SharedAlarmTag = propertyOffset(cls, "SharedAlarmTag");
                offsets_pawn.carriedDecoration = propertyOffset(cls, "carriedDecoration");
                offsets_pawn.PlayerReStartState = propertyOffset(cls, "PlayerReStartState");
                offsets_pawn.MenuName = propertyOffset(cls, "MenuName");
                offsets_pawn.NameArticle = propertyOffset(cls, "NameArticle");
                offsets_pawn.VoicePitch = propertyOffset(cls, "VoicePitch");
                offsets_pawn.VoiceType = propertyOffset(cls, "VoiceType");
                offsets_pawn.OldMessageTime = propertyOffset(cls, "OldMessageTime");
                offsets_pawn.RouteCache = propertyOffset(cls, "RouteCache");
                offsets_pawn.PlayerReplicationInfoClass = propertyOffset(cls, "PlayerReplicationInfoClass");
                offsets_pawn.PlayerReplicationInfo = propertyOffset(cls, "PlayerReplicationInfo");
                offsets_pawn.Shadow = propertyOffset(cls, "Shadow");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.PlayerPawn");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.PlayerPawn\n");
            } else {
                offsets_playerpawn.Player = propertyOffset(cls, "Player");
                offsets_playerpawn.Password = propertyOffset(cls, "Password");
                offsets_playerpawn.DodgeClickTimer = propertyOffset(cls, "DodgeClickTimer");
                offsets_playerpawn.DodgeClickTime = propertyOffset(cls, "DodgeClickTime");
                offsets_playerpawn.Bob = propertyOffset(cls, "Bob");
                offsets_playerpawn.LandBob = propertyOffset(cls, "LandBob");
                offsets_playerpawn.AppliedBob = propertyOffset(cls, "AppliedBob");
                offsets_playerpawn.bobtime = propertyOffset(cls, "bobtime");
                offsets_playerpawn.ShowFlags = propertyOffset(cls, "ShowFlags");
                offsets_playerpawn.RendMap = propertyOffset(cls, "RendMap");
                offsets_playerpawn.Misc1 = propertyOffset(cls, "Misc1");
                offsets_playerpawn.Misc2 = propertyOffset(cls, "Misc2");
                offsets_playerpawn.ViewTarget = propertyOffset(cls, "ViewTarget");
                offsets_playerpawn.FlashScale = propertyOffset(cls, "FlashScale");
                offsets_playerpawn.FlashFog = propertyOffset(cls, "FlashFog");
                offsets_playerpawn.myHUD = propertyOffset(cls, "myHUD");
                offsets_playerpawn.Scoring = propertyOffset(cls, "Scoring");
                offsets_playerpawn.HUDType = propertyOffset(cls, "HUDType");
                offsets_playerpawn.ScoringType = propertyOffset(cls, "ScoringType");
                offsets_playerpawn.DesiredFlashScale = propertyOffset(cls, "DesiredFlashScale");
                offsets_playerpawn.ConstantGlowScale = propertyOffset(cls, "ConstantGlowScale");
                offsets_playerpawn.InstantFlash = propertyOffset(cls, "InstantFlash");
                offsets_playerpawn.DesiredFlashFog = propertyOffset(cls, "DesiredFlashFog");
                offsets_playerpawn.ConstantGlowFog = propertyOffset(cls, "ConstantGlowFog");
                offsets_playerpawn.InstantFog = propertyOffset(cls, "InstantFog");
                offsets_playerpawn.DesiredFOV = propertyOffset(cls, "DesiredFOV");
                offsets_playerpawn.DefaultFOV = propertyOffset(cls, "DefaultFOV");
                offsets_playerpawn.Song = propertyOffset(cls, "Song");
                offsets_playerpawn.SongSection = propertyOffset(cls, "SongSection");
                offsets_playerpawn.CdTrack = propertyOffset(cls, "CdTrack");
                offsets_playerpawn.Transition = propertyOffset(cls, "Transition");
                offsets_playerpawn.shaketimer = propertyOffset(cls, "shaketimer");
                offsets_playerpawn.shakemag = propertyOffset(cls, "shakemag");
                offsets_playerpawn.shakevert = propertyOffset(cls, "shakevert");
                offsets_playerpawn.maxshake = propertyOffset(cls, "maxshake");
                offsets_playerpawn.verttimer = propertyOffset(cls, "verttimer");
                offsets_playerpawn.CarcassType = propertyOffset(cls, "CarcassType");
                offsets_playerpawn.MyAutoAim = propertyOffset(cls, "MyAutoAim");
                offsets_playerpawn.Handedness = propertyOffset(cls, "Handedness");
                offsets_playerpawn.JumpSound = propertyOffset(cls, "JumpSound");
                offsets_playerpawn.bAdmin = propertyOffset(cls, "bAdmin");
                offsets_playerpawn.bLookUpStairs = propertyOffset(cls, "bLookUpStairs");
                offsets_playerpawn.bSnapToLevel = propertyOffset(cls, "bSnapToLevel");
                offsets_playerpawn.bAlwaysMouseLook = propertyOffset(cls, "bAlwaysMouseLook");
                offsets_playerpawn.bKeyboardLook = propertyOffset(cls, "bKeyboardLook");
                offsets_playerpawn.bWasForward = propertyOffset(cls, "bWasForward");
                offsets_playerpawn.bWasBack = propertyOffset(cls, "bWasBack");
                offsets_playerpawn.bWasLeft = propertyOffset(cls, "bWasLeft");
                offsets_playerpawn.bWasRight = propertyOffset(cls, "bWasRight");
                offsets_playerpawn.bEdgeForward = propertyOffset(cls, "bEdgeForward");
                offsets_playerpawn.bEdgeBack = propertyOffset(cls, "bEdgeBack");
                offsets_playerpawn.bEdgeLeft = propertyOffset(cls, "bEdgeLeft");
                offsets_playerpawn.bEdgeRight = propertyOffset(cls, "bEdgeRight");
                offsets_playerpawn.bIsCrouching = propertyOffset(cls, "bIsCrouching");
                offsets_playerpawn.bShakeDir = propertyOffset(cls, "bShakeDir");
                offsets_playerpawn.bAnimTransition = propertyOffset(cls, "bAnimTransition");
                offsets_playerpawn.bIsTurning = propertyOffset(cls, "bIsTurning");
                offsets_playerpawn.bFrozen = propertyOffset(cls, "bFrozen");
                offsets_playerpawn.bBadConnectionAlert = propertyOffset(cls, "bBadConnectionAlert");
                offsets_playerpawn.bInvertMouse = propertyOffset(cls, "bInvertMouse");
                offsets_playerpawn.bShowScores = propertyOffset(cls, "bShowScores");
                offsets_playerpawn.bShowMenu = propertyOffset(cls, "bShowMenu");
                offsets_playerpawn.bSpecialMenu = propertyOffset(cls, "bSpecialMenu");
                offsets_playerpawn.bWokeUp = propertyOffset(cls, "bWokeUp");
                offsets_playerpawn.bPressedJump = propertyOffset(cls, "bPressedJump");
                offsets_playerpawn.bUpdatePosition = propertyOffset(cls, "bUpdatePosition");
                offsets_playerpawn.bDelayedCommand = propertyOffset(cls, "bDelayedCommand");
                offsets_playerpawn.bRising = propertyOffset(cls, "bRising");
                offsets_playerpawn.bReducedVis = propertyOffset(cls, "bReducedVis");
                offsets_playerpawn.bCenterView = propertyOffset(cls, "bCenterView");
                offsets_playerpawn.bMaxMouseSmoothing = propertyOffset(cls, "bMaxMouseSmoothing");
                offsets_playerpawn.bMouseZeroed = propertyOffset(cls, "bMouseZeroed");
                offsets_playerpawn.bReadyToPlay = propertyOffset(cls, "bReadyToPlay");
                offsets_playerpawn.bNoFlash = propertyOffset(cls, "bNoFlash");
                offsets_playerpawn.bNoVoices = propertyOffset(cls, "bNoVoices");
                offsets_playerpawn.bMessageBeep = propertyOffset(cls, "bMessageBeep");
                offsets_playerpawn.bZooming = propertyOffset(cls, "bZooming");
                offsets_playerpawn.bSinglePlayer = propertyOffset(cls, "bSinglePlayer");
                offsets_playerpawn.bJustFired = propertyOffset(cls, "bJustFired");
                offsets_playerpawn.bJustAltFired = propertyOffset(cls, "bJustAltFired");
                offsets_playerpawn.bIsTyping = propertyOffset(cls, "bIsTyping");
                offsets_playerpawn.bFixedCamera = propertyOffset(cls, "bFixedCamera");
                offsets_playerpawn.bNeverAutoSwitch = propertyOffset(cls, "bNeverAutoSwitch");
                offsets_playerpawn.bJumpStatus = propertyOffset(cls, "bJumpStatus");
                offsets_playerpawn.bUpdating = propertyOffset(cls, "bUpdating");
                offsets_playerpawn.bCheatsEnabled = propertyOffset(cls, "bCheatsEnabled");
                offsets_playerpawn.ZoomLevel = propertyOffset(cls, "ZoomLevel");
                offsets_playerpawn.SpecialMenu = propertyOffset(cls, "SpecialMenu");
                offsets_playerpawn.DelayedCommand = propertyOffset(cls, "DelayedCommand");
                offsets_playerpawn.MouseSensitivity = propertyOffset(cls, "MouseSensitivity");
                offsets_playerpawn.WeaponPriority = propertyOffset(cls, "WeaponPriority");
                offsets_playerpawn.SmoothMouseX = propertyOffset(cls, "SmoothMouseX");
                offsets_playerpawn.SmoothMouseY = propertyOffset(cls, "SmoothMouseY");
                offsets_playerpawn.BorrowedMouseX = propertyOffset(cls, "BorrowedMouseX");
                offsets_playerpawn.BorrowedMouseY = propertyOffset(cls, "BorrowedMouseY");
                offsets_playerpawn.MouseSmoothThreshold = propertyOffset(cls, "MouseSmoothThreshold");
                offsets_playerpawn.MouseZeroTime = propertyOffset(cls, "MouseZeroTime");
                offsets_playerpawn.aBaseX = propertyOffset(cls, "aBaseX");
                offsets_playerpawn.aBaseY = propertyOffset(cls, "aBaseY");
                offsets_playerpawn.aBaseZ = propertyOffset(cls, "aBaseZ");
                offsets_playerpawn.aMouseX = propertyOffset(cls, "aMouseX");
                offsets_playerpawn.aMouseY = propertyOffset(cls, "aMouseY");
                offsets_playerpawn.aForward = propertyOffset(cls, "aForward");
                offsets_playerpawn.aTurn = propertyOffset(cls, "aTurn");
                offsets_playerpawn.aStrafe = propertyOffset(cls, "aStrafe");
                offsets_playerpawn.aUp = propertyOffset(cls, "aUp");
                offsets_playerpawn.aLookUp = propertyOffset(cls, "aLookUp");
                offsets_playerpawn.aExtra4 = propertyOffset(cls, "aExtra4");
                offsets_playerpawn.aExtra3 = propertyOffset(cls, "aExtra3");
                offsets_playerpawn.aExtra2 = propertyOffset(cls, "aExtra2");
                offsets_playerpawn.aExtra1 = propertyOffset(cls, "aExtra1");
                offsets_playerpawn.aExtra0 = propertyOffset(cls, "aExtra0");
                offsets_playerpawn.SavedMoves = propertyOffset(cls, "SavedMoves");
                offsets_playerpawn.FreeMoves = propertyOffset(cls, "FreeMoves");
                offsets_playerpawn.PendingMove = propertyOffset(cls, "PendingMove");
                offsets_playerpawn.CurrentTimeStamp = propertyOffset(cls, "CurrentTimeStamp");
                offsets_playerpawn.LastUpdateTime = propertyOffset(cls, "LastUpdateTime");
                offsets_playerpawn.ServerTimeStamp = propertyOffset(cls, "ServerTimeStamp");
                offsets_playerpawn.TimeMargin = propertyOffset(cls, "TimeMargin");
                offsets_playerpawn.ClientUpdateTime = propertyOffset(cls, "ClientUpdateTime");
                offsets_playerpawn.MaxTimeMargin = propertyOffset(cls, "MaxTimeMargin");
                offsets_playerpawn.ProgressMessage = propertyOffset(cls, "ProgressMessage");
                offsets_playerpawn.ProgressColor = propertyOffset(cls, "ProgressColor");
                offsets_playerpawn.ProgressTimeOut = propertyOffset(cls, "ProgressTimeOut");
                offsets_playerpawn.QuickSaveString = propertyOffset(cls, "QuickSaveString");
                offsets_playerpawn.NoPauseMessage = propertyOffset(cls, "NoPauseMessage");
                offsets_playerpawn.ViewingFrom = propertyOffset(cls, "ViewingFrom");
                offsets_playerpawn.OwnCamera = propertyOffset(cls, "OwnCamera");
                offsets_playerpawn.FailedView = propertyOffset(cls, "FailedView");
                offsets_playerpawn.GameReplicationInfo = propertyOffset(cls, "GameReplicationInfo");
                offsets_playerpawn.ngWorldSecret = propertyOffset(cls, "ngWorldSecret");
                offsets_playerpawn.ngSecretSet = propertyOffset(cls, "ngSecretSet");
                offsets_playerpawn.TargetViewRotation = propertyOffset(cls, "TargetViewRotation");
                offsets_playerpawn.TargetEyeHeight = propertyOffset(cls, "TargetEyeHeight");
                offsets_playerpawn.TargetWeaponViewOffset = propertyOffset(cls, "TargetWeaponViewOffset");
                offsets_playerpawn.DemoViewPitch = propertyOffset(cls, "DemoViewPitch");
                offsets_playerpawn.DemoViewYaw = propertyOffset(cls, "DemoViewYaw");
                offsets_playerpawn.LastPlaySound = propertyOffset(cls, "LastPlaySound");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.LevelInfo");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.LevelInfo\n");
            } else {
                offsets_levelinfo.TimeDilation = propertyOffset(cls, "TimeDilation");
                offsets_levelinfo.TimeSeconds = propertyOffset(cls, "TimeSeconds");
                offsets_levelinfo.Year = propertyOffset(cls, "Year");
                offsets_levelinfo.Month = propertyOffset(cls, "Month");
                offsets_levelinfo.Day = propertyOffset(cls, "Day");
                offsets_levelinfo.DayOfWeek = propertyOffset(cls, "DayOfWeek");
                offsets_levelinfo.Hour = propertyOffset(cls, "Hour");
                offsets_levelinfo.Minute = propertyOffset(cls, "Minute");
                offsets_levelinfo.Second = propertyOffset(cls, "Second");
                offsets_levelinfo.Millisecond = propertyOffset(cls, "Millisecond");
                offsets_levelinfo.Title = propertyOffset(cls, "Title");
                offsets_levelinfo.Author = propertyOffset(cls, "Author");
                offsets_levelinfo.IdealPlayerCount = propertyOffset(cls, "IdealPlayerCount");
                offsets_levelinfo.RecommendedEnemies = propertyOffset(cls, "RecommendedEnemies");
                offsets_levelinfo.RecommendedTeammates = propertyOffset(cls, "RecommendedTeammates");
                offsets_levelinfo.LevelEnterText = propertyOffset(cls, "LevelEnterText");
                offsets_levelinfo.LocalizedPkg = propertyOffset(cls, "LocalizedPkg");
                offsets_levelinfo.Pauser = propertyOffset(cls, "Pauser");
                offsets_levelinfo.Summary = propertyOffset(cls, "Summary");
                offsets_levelinfo.bLonePlayer = propertyOffset(cls, "bLonePlayer");
                offsets_levelinfo.bBegunPlay = propertyOffset(cls, "bBegunPlay");
                offsets_levelinfo.bPlayersOnly = propertyOffset(cls, "bPlayersOnly");
                offsets_levelinfo.bHighDetailMode = propertyOffset(cls, "bHighDetailMode");
                offsets_levelinfo.bDropDetail = propertyOffset(cls, "bDropDetail");
                offsets_levelinfo.bAggressiveLOD = propertyOffset(cls, "bAggressiveLOD");
                offsets_levelinfo.bStartup = propertyOffset(cls, "bStartup");
                offsets_levelinfo.bHumansOnly = propertyOffset(cls, "bHumansOnly");
                offsets_levelinfo.bNoCheating = propertyOffset(cls, "bNoCheating");
                offsets_levelinfo.bAllowFOV = propertyOffset(cls, "bAllowFOV");
                offsets_levelinfo.Song = propertyOffset(cls, "Song");
                offsets_levelinfo.SongSection = propertyOffset(cls, "SongSection");
                offsets_levelinfo.CdTrack = propertyOffset(cls, "CdTrack");
                offsets_levelinfo.PlayerDoppler = propertyOffset(cls, "PlayerDoppler");
                offsets_levelinfo.Brightness = propertyOffset(cls, "Brightness");
                offsets_levelinfo.Screenshot = propertyOffset(cls, "Screenshot");
                offsets_levelinfo.DefaultTexture = propertyOffset(cls, "DefaultTexture");
                offsets_levelinfo.HubStackLevel = propertyOffset(cls, "HubStackLevel");
                offsets_levelinfo.LevelAction = propertyOffset(cls, "LevelAction");
                offsets_levelinfo.bNeverPrecache = propertyOffset(cls, "bNeverPrecache");
                offsets_levelinfo.NetMode = propertyOffset(cls, "NetMode");
                offsets_levelinfo.ComputerName = propertyOffset(cls, "ComputerName");
                offsets_levelinfo.EngineVersion = propertyOffset(cls, "EngineVersion");
                offsets_levelinfo.MinNetVersion = propertyOffset(cls, "MinNetVersion");
                offsets_levelinfo.DefaultGameType = propertyOffset(cls, "DefaultGameType");
                offsets_levelinfo.Game = propertyOffset(cls, "Game");
                offsets_levelinfo.NavigationPointList = propertyOffset(cls, "NavigationPointList");
                offsets_levelinfo.PawnList = propertyOffset(cls, "PawnList");
                offsets_levelinfo.NextURL = propertyOffset(cls, "NextURL");
                offsets_levelinfo.bNextItems = propertyOffset(cls, "bNextItems");
                offsets_levelinfo.NextSwitchCountdown = propertyOffset(cls, "NextSwitchCountdown");
                offsets_levelinfo.AIProfile = propertyOffset(cls, "AIProfile");
                offsets_levelinfo.AvgAITime = propertyOffset(cls, "AvgAITime");
                offsets_levelinfo.bCheckWalkSurfaces = propertyOffset(cls, "bCheckWalkSurfaces");
                offsets_levelinfo.SpawnNotify = propertyOffset(cls, "SpawnNotify");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.ZoneInfo");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.ZoneInfo\n");
            } else {
                offsets_zoneinfo.ZoneTag = propertyOffset(cls, "ZoneTag");
                offsets_zoneinfo.ZoneGravity = propertyOffset(cls, "ZoneGravity");
                offsets_zoneinfo.ZoneVelocity = propertyOffset(cls, "ZoneVelocity");
                offsets_zoneinfo.ZoneGroundFriction = propertyOffset(cls, "ZoneGroundFriction");
                offsets_zoneinfo.ZoneFluidFriction = propertyOffset(cls, "ZoneFluidFriction");
                offsets_zoneinfo.ZoneTerminalVelocity = propertyOffset(cls, "ZoneTerminalVelocity");
                offsets_zoneinfo.ZonePlayerEvent = propertyOffset(cls, "ZonePlayerEvent");
                offsets_zoneinfo.ZonePlayerCount = propertyOffset(cls, "ZonePlayerCount");
                offsets_zoneinfo.NumCarcasses = propertyOffset(cls, "NumCarcasses");
                offsets_zoneinfo.DamagePerSec = propertyOffset(cls, "DamagePerSec");
                offsets_zoneinfo.DamageType = propertyOffset(cls, "DamageType");
                offsets_zoneinfo.DamageString = propertyOffset(cls, "DamageString");
                offsets_zoneinfo.ZoneName = propertyOffset(cls, "ZoneName");
                offsets_zoneinfo.locationid = propertyOffset(cls, "locationid");
                offsets_zoneinfo.MaxCarcasses = propertyOffset(cls, "MaxCarcasses");
                offsets_zoneinfo.EntrySound = propertyOffset(cls, "EntrySound");
                offsets_zoneinfo.ExitSound = propertyOffset(cls, "ExitSound");
                offsets_zoneinfo.EntryActor = propertyOffset(cls, "EntryActor");
                offsets_zoneinfo.ExitActor = propertyOffset(cls, "ExitActor");
                offsets_zoneinfo.SkyZone = propertyOffset(cls, "SkyZone");
                offsets_zoneinfo.bWaterZone = propertyOffset(cls, "bWaterZone");
                offsets_zoneinfo.bFogZone = propertyOffset(cls, "bFogZone");
                offsets_zoneinfo.bKillZone = propertyOffset(cls, "bKillZone");
                offsets_zoneinfo.bNeutralZone = propertyOffset(cls, "bNeutralZone");
                offsets_zoneinfo.bGravityZone = propertyOffset(cls, "bGravityZone");
                offsets_zoneinfo.bPainZone = propertyOffset(cls, "bPainZone");
                offsets_zoneinfo.bDestructive = propertyOffset(cls, "bDestructive");
                offsets_zoneinfo.bNoInventory = propertyOffset(cls, "bNoInventory");
                offsets_zoneinfo.bMoveProjectiles = propertyOffset(cls, "bMoveProjectiles");
                offsets_zoneinfo.bBounceVelocity = propertyOffset(cls, "bBounceVelocity");
                offsets_zoneinfo.AmbientBrightness = propertyOffset(cls, "AmbientBrightness");
                offsets_zoneinfo.AmbientHue = propertyOffset(cls, "AmbientHue");
                offsets_zoneinfo.AmbientSaturation = propertyOffset(cls, "AmbientSaturation");
                offsets_zoneinfo.FogColor = propertyOffset(cls, "FogColor");
                offsets_zoneinfo.FogDistance = propertyOffset(cls, "FogDistance");
                offsets_zoneinfo.EnvironmentMap = propertyOffset(cls, "EnvironmentMap");
                offsets_zoneinfo.TexUPanSpeed = propertyOffset(cls, "TexUPanSpeed");
                offsets_zoneinfo.TexVPanSpeed = propertyOffset(cls, "TexVPanSpeed");
                offsets_zoneinfo.ViewFlash = propertyOffset(cls, "ViewFlash");
                offsets_zoneinfo.ViewFog = propertyOffset(cls, "ViewFog");
                offsets_zoneinfo.bReverbZone = propertyOffset(cls, "bReverbZone");
                offsets_zoneinfo.bRaytraceReverb = propertyOffset(cls, "bRaytraceReverb");
                offsets_zoneinfo.SpeedOfSound = propertyOffset(cls, "SpeedOfSound");
                offsets_zoneinfo.MasterGain = propertyOffset(cls, "MasterGain");
                offsets_zoneinfo.CutoffHz = propertyOffset(cls, "CutoffHz");
                offsets_zoneinfo.Delay = propertyOffset(cls, "Delay");
                offsets_zoneinfo.Gain = propertyOffset(cls, "Gain");
                offsets_zoneinfo.LensFlare = propertyOffset(cls, "LensFlare");
                offsets_zoneinfo.LensFlareOffset = propertyOffset(cls, "LensFlareOffset");
                offsets_zoneinfo.LensFlareScale = propertyOffset(cls, "LensFlareScale");
                offsets_zoneinfo.MinLightCount = propertyOffset(cls, "MinLightCount");
                offsets_zoneinfo.MaxLightCount = propertyOffset(cls, "MaxLightCount");
                offsets_zoneinfo.MinLightingPolyCount = propertyOffset(cls, "MinLightingPolyCount");
                offsets_zoneinfo.MaxLightingPolyCount = propertyOffset(cls, "MaxLightingPolyCount");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.SkyZoneInfo");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.SkyZoneInfo\n");
            } else {
            }
        }
        {
            UClass* cls = objects::findClass("Engine.WarpZoneInfo");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.WarpZoneInfo\n");
            } else {
                offsets_warpzoneinfo.OtherSideURL = propertyOffset(cls, "OtherSideURL");
                offsets_warpzoneinfo.ThisTag = propertyOffset(cls, "ThisTag");
                offsets_warpzoneinfo.bNoTeleFrag = propertyOffset(cls, "bNoTeleFrag");
                offsets_warpzoneinfo.iWarpZone = propertyOffset(cls, "iWarpZone");
                offsets_warpzoneinfo.WarpCoords = propertyOffset(cls, "WarpCoords");
                offsets_warpzoneinfo.OtherSideActor = propertyOffset(cls, "OtherSideActor");
                offsets_warpzoneinfo.OtherSideLevel = propertyOffset(cls, "OtherSideLevel");
                offsets_warpzoneinfo.Destinations = propertyOffset(cls, "Destinations");
                offsets_warpzoneinfo.numDestinations = propertyOffset(cls, "numDestinations");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.GameInfo");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.GameInfo\n");
            } else {
                offsets_gameinfo.ItemGoals = propertyOffset(cls, "ItemGoals");
                offsets_gameinfo.KillGoals = propertyOffset(cls, "KillGoals");
                offsets_gameinfo.SecretGoals = propertyOffset(cls, "SecretGoals");
                offsets_gameinfo.Difficulty = propertyOffset(cls, "Difficulty");
                offsets_gameinfo.bNoMonsters = propertyOffset(cls, "bNoMonsters");
                offsets_gameinfo.bMuteSpectators = propertyOffset(cls, "bMuteSpectators");
                offsets_gameinfo.bHumansOnly = propertyOffset(cls, "bHumansOnly");
                offsets_gameinfo.bRestartLevel = propertyOffset(cls, "bRestartLevel");
                offsets_gameinfo.bPauseable = propertyOffset(cls, "bPauseable");
                offsets_gameinfo.bCoopWeaponMode = propertyOffset(cls, "bCoopWeaponMode");
                offsets_gameinfo.bClassicDeathMessages = propertyOffset(cls, "bClassicDeathMessages");
                offsets_gameinfo.bLowGore = propertyOffset(cls, "bLowGore");
                offsets_gameinfo.bCanChangeSkin = propertyOffset(cls, "bCanChangeSkin");
                offsets_gameinfo.bTeamGame = propertyOffset(cls, "bTeamGame");
                offsets_gameinfo.bVeryLowGore = propertyOffset(cls, "bVeryLowGore");
                offsets_gameinfo.bNoCheating = propertyOffset(cls, "bNoCheating");
                offsets_gameinfo.bAllowFOV = propertyOffset(cls, "bAllowFOV");
                offsets_gameinfo.bDeathMatch = propertyOffset(cls, "bDeathMatch");
                offsets_gameinfo.bGameEnded = propertyOffset(cls, "bGameEnded");
                offsets_gameinfo.bOverTime = propertyOffset(cls, "bOverTime");
                offsets_gameinfo.bAlternateMode = propertyOffset(cls, "bAlternateMode");
                offsets_gameinfo.bCanViewOthers = propertyOffset(cls, "bCanViewOthers");
                offsets_gameinfo.AutoAim = propertyOffset(cls, "AutoAim");
                offsets_gameinfo.GameSpeed = propertyOffset(cls, "GameSpeed");
                offsets_gameinfo.StartTime = propertyOffset(cls, "StartTime");
                offsets_gameinfo.DefaultPlayerClass = propertyOffset(cls, "DefaultPlayerClass");
                offsets_gameinfo.DefaultWeapon = propertyOffset(cls, "DefaultWeapon");
                offsets_gameinfo.MaxSpectators = propertyOffset(cls, "MaxSpectators");
                offsets_gameinfo.NumSpectators = propertyOffset(cls, "NumSpectators");
                offsets_gameinfo.AdminPassword = propertyOffset(cls, "AdminPassword");
                offsets_gameinfo.GamePassword = propertyOffset(cls, "GamePassword");
                offsets_gameinfo.ScoreBoardType = propertyOffset(cls, "ScoreBoardType");
                offsets_gameinfo.GameMenuType = propertyOffset(cls, "GameMenuType");
                offsets_gameinfo.BotMenuType = propertyOffset(cls, "BotMenuType");
                offsets_gameinfo.RulesMenuType = propertyOffset(cls, "RulesMenuType");
                offsets_gameinfo.SettingsMenuType = propertyOffset(cls, "SettingsMenuType");
                offsets_gameinfo.GameUMenuType = propertyOffset(cls, "GameUMenuType");
                offsets_gameinfo.MultiplayerUMenuType = propertyOffset(cls, "MultiplayerUMenuType");
                offsets_gameinfo.GameOptionsMenuType = propertyOffset(cls, "GameOptionsMenuType");
                offsets_gameinfo.HUDType = propertyOffset(cls, "HUDType");
                offsets_gameinfo.MapListType = propertyOffset(cls, "MapListType");
                offsets_gameinfo.MapPrefix = propertyOffset(cls, "MapPrefix");
                offsets_gameinfo.BeaconName = propertyOffset(cls, "BeaconName");
                offsets_gameinfo.SpecialDamageString = propertyOffset(cls, "SpecialDamageString");
                offsets_gameinfo.SwitchLevelMessage = propertyOffset(cls, "SwitchLevelMessage");
                offsets_gameinfo.SentText = propertyOffset(cls, "SentText");
                offsets_gameinfo.DefaultPlayerName = propertyOffset(cls, "DefaultPlayerName");
                offsets_gameinfo.LeftMessage = propertyOffset(cls, "LeftMessage");
                offsets_gameinfo.FailedSpawnMessage = propertyOffset(cls, "FailedSpawnMessage");
                offsets_gameinfo.FailedPlaceMessage = propertyOffset(cls, "FailedPlaceMessage");
                offsets_gameinfo.FailedTeamMessage = propertyOffset(cls, "FailedTeamMessage");
                offsets_gameinfo.NameChangedMessage = propertyOffset(cls, "NameChangedMessage");
                offsets_gameinfo.EnteredMessage = propertyOffset(cls, "EnteredMessage");
                offsets_gameinfo.GameName = propertyOffset(cls, "GameName");
                offsets_gameinfo.MaxedOutMessage = propertyOffset(cls, "MaxedOutMessage");
                offsets_gameinfo.WrongPassword = propertyOffset(cls, "WrongPassword");
                offsets_gameinfo.NeedPassword = propertyOffset(cls, "NeedPassword");
                offsets_gameinfo.IPBanned = propertyOffset(cls, "IPBanned");
                offsets_gameinfo.MaxPlayers = propertyOffset(cls, "MaxPlayers");
                offsets_gameinfo.NumPlayers = propertyOffset(cls, "NumPlayers");
                offsets_gameinfo.CurrentID = propertyOffset(cls, "CurrentID");
                offsets_gameinfo.IPPolicies = propertyOffset(cls, "IPPolicies");
                offsets_gameinfo.DeathMessageClass = propertyOffset(cls, "DeathMessageClass");
                offsets_gameinfo.DMMessageClass = propertyOffset(cls, "DMMessageClass");
                offsets_gameinfo.MutatorClass = propertyOffset(cls, "MutatorClass");
                offsets_gameinfo.BaseMutator = propertyOffset(cls, "BaseMutator");
                offsets_gameinfo.DamageMutator = propertyOffset(cls, "DamageMutator");
                offsets_gameinfo.WaterZoneType = propertyOffset(cls, "WaterZoneType");
                offsets_gameinfo.DefaultPlayerState = propertyOffset(cls, "DefaultPlayerState");
                offsets_gameinfo.GameReplicationInfoClass = propertyOffset(cls, "GameReplicationInfoClass");
                offsets_gameinfo.GameReplicationInfo = propertyOffset(cls, "GameReplicationInfo");
                offsets_gameinfo.ServerLogName = propertyOffset(cls, "ServerLogName");
                offsets_gameinfo.LocalLog = propertyOffset(cls, "LocalLog");
                offsets_gameinfo.WorldLog = propertyOffset(cls, "WorldLog");
                offsets_gameinfo.bLocalLog = propertyOffset(cls, "bLocalLog");
                offsets_gameinfo.bWorldLog = propertyOffset(cls, "bWorldLog");
                offsets_gameinfo.bBatchLocal = propertyOffset(cls, "bBatchLocal");
                offsets_gameinfo.bLoggingGame = propertyOffset(cls, "bLoggingGame");
                offsets_gameinfo.LocalLogFileName = propertyOffset(cls, "LocalLogFileName");
                offsets_gameinfo.WorldLogFileName = propertyOffset(cls, "WorldLogFileName");
                offsets_gameinfo.StatLogClass = propertyOffset(cls, "StatLogClass");
                offsets_gameinfo.DemoBuild = propertyOffset(cls, "DemoBuild");
                offsets_gameinfo.DemoHasTuts = propertyOffset(cls, "DemoHasTuts");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Brush");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Brush\n");
            } else {
                offsets_brush.CsgOper = propertyOffset(cls, "CsgOper");
                offsets_brush.UnusedLightMesh = propertyOffset(cls, "UnusedLightMesh");
                offsets_brush.PostPivot = propertyOffset(cls, "PostPivot");
                offsets_brush.MainScale = propertyOffset(cls, "MainScale");
                offsets_brush.PostScale = propertyOffset(cls, "PostScale");
                offsets_brush.TempScale = propertyOffset(cls, "TempScale");
                offsets_brush.BrushColor = propertyOffset(cls, "BrushColor");
                offsets_brush.PolyFlags = propertyOffset(cls, "PolyFlags");
                offsets_brush.bColored = propertyOffset(cls, "bColored");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Mover");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Mover\n");
            } else {
                offsets_mover.MoverEncroachType = propertyOffset(cls, "MoverEncroachType");
                offsets_mover.MoverGlideType = propertyOffset(cls, "MoverGlideType");
                offsets_mover.BumpType = propertyOffset(cls, "BumpType");
                offsets_mover.KeyNum = propertyOffset(cls, "KeyNum");
                offsets_mover.PrevKeyNum = propertyOffset(cls, "PrevKeyNum");
                offsets_mover.NumKeys = propertyOffset(cls, "NumKeys");
                offsets_mover.WorldRaytraceKey = propertyOffset(cls, "WorldRaytraceKey");
                offsets_mover.BrushRaytraceKey = propertyOffset(cls, "BrushRaytraceKey");
                offsets_mover.MoveTime = propertyOffset(cls, "MoveTime");
                offsets_mover.StayOpenTime = propertyOffset(cls, "StayOpenTime");
                offsets_mover.OtherTime = propertyOffset(cls, "OtherTime");
                offsets_mover.EncroachDamage = propertyOffset(cls, "EncroachDamage");
                offsets_mover.bTriggerOnceOnly = propertyOffset(cls, "bTriggerOnceOnly");
                offsets_mover.bSlave = propertyOffset(cls, "bSlave");
                offsets_mover.bUseTriggered = propertyOffset(cls, "bUseTriggered");
                offsets_mover.bDamageTriggered = propertyOffset(cls, "bDamageTriggered");
                offsets_mover.bDynamicLightMover = propertyOffset(cls, "bDynamicLightMover");
                offsets_mover.PlayerBumpEvent = propertyOffset(cls, "PlayerBumpEvent");
                offsets_mover.BumpEvent = propertyOffset(cls, "BumpEvent");
                offsets_mover.SavedTrigger = propertyOffset(cls, "SavedTrigger");
                offsets_mover.DamageThreshold = propertyOffset(cls, "DamageThreshold");
                offsets_mover.numTriggerEvents = propertyOffset(cls, "numTriggerEvents");
                offsets_mover.Leader = propertyOffset(cls, "Leader");
                offsets_mover.Follower = propertyOffset(cls, "Follower");
                offsets_mover.ReturnGroup = propertyOffset(cls, "ReturnGroup");
                offsets_mover.DelayTime = propertyOffset(cls, "DelayTime");
                offsets_mover.OpeningSound = propertyOffset(cls, "OpeningSound");
                offsets_mover.OpenedSound = propertyOffset(cls, "OpenedSound");
                offsets_mover.ClosingSound = propertyOffset(cls, "ClosingSound");
                offsets_mover.ClosedSound = propertyOffset(cls, "ClosedSound");
                offsets_mover.MoveAmbientSound = propertyOffset(cls, "MoveAmbientSound");
                offsets_mover.KeyPos = propertyOffset(cls, "KeyPos");
                offsets_mover.KeyRot = propertyOffset(cls, "KeyRot");
                offsets_mover.BasePos = propertyOffset(cls, "BasePos");
                offsets_mover.OldPos = propertyOffset(cls, "OldPos");
                offsets_mover.OldPrePivot = propertyOffset(cls, "OldPrePivot");
                offsets_mover.SavedPos = propertyOffset(cls, "SavedPos");
                offsets_mover.BaseRot = propertyOffset(cls, "BaseRot");
                offsets_mover.OldRot = propertyOffset(cls, "OldRot");
                offsets_mover.SavedRot = propertyOffset(cls, "SavedRot");
                offsets_mover.myMarker = propertyOffset(cls, "myMarker");
                offsets_mover.TriggerActor = propertyOffset(cls, "TriggerActor");
                offsets_mover.TriggerActor2 = propertyOffset(cls, "TriggerActor2");
                offsets_mover.WaitingPawn = propertyOffset(cls, "WaitingPawn");
                offsets_mover.bOpening = propertyOffset(cls, "bOpening");
                offsets_mover.bDelaying = propertyOffset(cls, "bDelaying");
                offsets_mover.bClientPause = propertyOffset(cls, "bClientPause");
                offsets_mover.bPlayerOnly = propertyOffset(cls, "bPlayerOnly");
                offsets_mover.RecommendedTrigger = propertyOffset(cls, "RecommendedTrigger");
                offsets_mover.SimOldPos = propertyOffset(cls, "SimOldPos");
                offsets_mover.SimOldRotPitch = propertyOffset(cls, "SimOldRotPitch");
                offsets_mover.SimOldRotYaw = propertyOffset(cls, "SimOldRotYaw");
                offsets_mover.SimOldRotRoll = propertyOffset(cls, "SimOldRotRoll");
                offsets_mover.SimInterpolate = propertyOffset(cls, "SimInterpolate");
                offsets_mover.RealPosition = propertyOffset(cls, "RealPosition");
                offsets_mover.RealRotation = propertyOffset(cls, "RealRotation");
                offsets_mover.ClientUpdate = propertyOffset(cls, "ClientUpdate");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.NavigationPoint");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.NavigationPoint\n");
            } else {
                offsets_navigationpoint.ownerTeam = propertyOffset(cls, "ownerTeam");
                offsets_navigationpoint.taken = propertyOffset(cls, "taken");
                offsets_navigationpoint.upstreamPaths = propertyOffset(cls, "upstreamPaths");
                offsets_navigationpoint.Paths = propertyOffset(cls, "Paths");
                offsets_navigationpoint.PrunedPaths = propertyOffset(cls, "PrunedPaths");
                offsets_navigationpoint.VisNoReachPaths = propertyOffset(cls, "VisNoReachPaths");
                offsets_navigationpoint.visitedWeight = propertyOffset(cls, "visitedWeight");
                offsets_navigationpoint.RouteCache = propertyOffset(cls, "RouteCache");
                offsets_navigationpoint.bestPathWeight = propertyOffset(cls, "bestPathWeight");
                offsets_navigationpoint.nextNavigationPoint = propertyOffset(cls, "nextNavigationPoint");
                offsets_navigationpoint.nextOrdered = propertyOffset(cls, "nextOrdered");
                offsets_navigationpoint.prevOrdered = propertyOffset(cls, "prevOrdered");
                offsets_navigationpoint.startPath = propertyOffset(cls, "startPath");
                offsets_navigationpoint.previousPath = propertyOffset(cls, "previousPath");
                offsets_navigationpoint.cost = propertyOffset(cls, "cost");
                offsets_navigationpoint.ExtraCost = propertyOffset(cls, "ExtraCost");
                offsets_navigationpoint.bPlayerOnly = propertyOffset(cls, "bPlayerOnly");
                offsets_navigationpoint.bEndPoint = propertyOffset(cls, "bEndPoint");
                offsets_navigationpoint.bEndPointOnly = propertyOffset(cls, "bEndPointOnly");
                offsets_navigationpoint.bSpecialCost = propertyOffset(cls, "bSpecialCost");
                offsets_navigationpoint.bOneWayPath = propertyOffset(cls, "bOneWayPath");
                offsets_navigationpoint.bNeverUseStrafing = propertyOffset(cls, "bNeverUseStrafing");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Inventory");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Inventory\n");
            } else {
                offsets_inventory.AutoSwitchPriority = propertyOffset(cls, "AutoSwitchPriority");
                offsets_inventory.InventoryGroup = propertyOffset(cls, "InventoryGroup");
                offsets_inventory.bActivatable = propertyOffset(cls, "bActivatable");
                offsets_inventory.bDisplayableInv = propertyOffset(cls, "bDisplayableInv");
                offsets_inventory.bActive = propertyOffset(cls, "bActive");
                offsets_inventory.bSleepTouch = propertyOffset(cls, "bSleepTouch");
                offsets_inventory.bHeldItem = propertyOffset(cls, "bHeldItem");
                offsets_inventory.bTossedOut = propertyOffset(cls, "bTossedOut");
                offsets_inventory.bAmbientGlow = propertyOffset(cls, "bAmbientGlow");
                offsets_inventory.bInstantRespawn = propertyOffset(cls, "bInstantRespawn");
                offsets_inventory.bRotatingPickup = propertyOffset(cls, "bRotatingPickup");
                offsets_inventory.PickupMessage = propertyOffset(cls, "PickupMessage");
                offsets_inventory.ItemName = propertyOffset(cls, "ItemName");
                offsets_inventory.ItemArticle = propertyOffset(cls, "ItemArticle");
                offsets_inventory.RespawnTime = propertyOffset(cls, "RespawnTime");
                offsets_inventory.PlayerLastTouched = propertyOffset(cls, "PlayerLastTouched");
                offsets_inventory.PlayerViewOffset = propertyOffset(cls, "PlayerViewOffset");
                offsets_inventory.PlayerViewMesh = propertyOffset(cls, "PlayerViewMesh");
                offsets_inventory.PlayerViewScale = propertyOffset(cls, "PlayerViewScale");
                offsets_inventory.BobDamping = propertyOffset(cls, "BobDamping");
                offsets_inventory.PickupViewMesh = propertyOffset(cls, "PickupViewMesh");
                offsets_inventory.PickupViewScale = propertyOffset(cls, "PickupViewScale");
                offsets_inventory.ThirdPersonMesh = propertyOffset(cls, "ThirdPersonMesh");
                offsets_inventory.ThirdPersonScale = propertyOffset(cls, "ThirdPersonScale");
                offsets_inventory.StatusIcon = propertyOffset(cls, "StatusIcon");
                offsets_inventory.ProtectionType1 = propertyOffset(cls, "ProtectionType1");
                offsets_inventory.ProtectionType2 = propertyOffset(cls, "ProtectionType2");
                offsets_inventory.Charge = propertyOffset(cls, "Charge");
                offsets_inventory.ArmorAbsorption = propertyOffset(cls, "ArmorAbsorption");
                offsets_inventory.bIsAnArmor = propertyOffset(cls, "bIsAnArmor");
                offsets_inventory.AbsorptionPriority = propertyOffset(cls, "AbsorptionPriority");
                offsets_inventory.NextArmor = propertyOffset(cls, "NextArmor");
                offsets_inventory.MaxDesireability = propertyOffset(cls, "MaxDesireability");
                offsets_inventory.myMarker = propertyOffset(cls, "myMarker");
                offsets_inventory.bSteadyFlash3rd = propertyOffset(cls, "bSteadyFlash3rd");
                offsets_inventory.bFirstFrame = propertyOffset(cls, "bFirstFrame");
                offsets_inventory.bMuzzleFlashParticles = propertyOffset(cls, "bMuzzleFlashParticles");
                offsets_inventory.bToggleSteadyFlash = propertyOffset(cls, "bToggleSteadyFlash");
                offsets_inventory.bSteadyToggle = propertyOffset(cls, "bSteadyToggle");
                offsets_inventory.FlashCount = propertyOffset(cls, "FlashCount");
                offsets_inventory.OldFlashCount = propertyOffset(cls, "OldFlashCount");
                offsets_inventory.MuzzleFlashStyle = propertyOffset(cls, "MuzzleFlashStyle");
                offsets_inventory.MuzzleFlashMesh = propertyOffset(cls, "MuzzleFlashMesh");
                offsets_inventory.MuzzleFlashScale = propertyOffset(cls, "MuzzleFlashScale");
                offsets_inventory.MuzzleFlashTexture = propertyOffset(cls, "MuzzleFlashTexture");
                offsets_inventory.PickupSound = propertyOffset(cls, "PickupSound");
                offsets_inventory.ActivateSound = propertyOffset(cls, "ActivateSound");
                offsets_inventory.DeActivateSound = propertyOffset(cls, "DeActivateSound");
                offsets_inventory.RespawnSound = propertyOffset(cls, "RespawnSound");
                offsets_inventory.Icon = propertyOffset(cls, "Icon");
                offsets_inventory.M_Activated = propertyOffset(cls, "M_Activated");
                offsets_inventory.M_Selected = propertyOffset(cls, "M_Selected");
                offsets_inventory.M_Deactivated = propertyOffset(cls, "M_Deactivated");
                offsets_inventory.PickupMessageClass = propertyOffset(cls, "PickupMessageClass");
                offsets_inventory.ItemMessageClass = propertyOffset(cls, "ItemMessageClass");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Weapon");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Weapon\n");
            } else {
                offsets_weapon.MaxTargetRange = propertyOffset(cls, "MaxTargetRange");
                offsets_weapon.AmmoName = propertyOffset(cls, "AmmoName");
                offsets_weapon.ReloadCount = propertyOffset(cls, "ReloadCount");
                offsets_weapon.PickupAmmoCount = propertyOffset(cls, "PickupAmmoCount");
                offsets_weapon.AmmoType = propertyOffset(cls, "AmmoType");
                offsets_weapon.bPointing = propertyOffset(cls, "bPointing");
                offsets_weapon.bInstantHit = propertyOffset(cls, "bInstantHit");
                offsets_weapon.bAltInstantHit = propertyOffset(cls, "bAltInstantHit");
                offsets_weapon.bWarnTarget = propertyOffset(cls, "bWarnTarget");
                offsets_weapon.bAltWarnTarget = propertyOffset(cls, "bAltWarnTarget");
                offsets_weapon.bWeaponUp = propertyOffset(cls, "bWeaponUp");
                offsets_weapon.bChangeWeapon = propertyOffset(cls, "bChangeWeapon");
                offsets_weapon.bLockedOn = propertyOffset(cls, "bLockedOn");
                offsets_weapon.bSplashDamage = propertyOffset(cls, "bSplashDamage");
                offsets_weapon.bCanThrow = propertyOffset(cls, "bCanThrow");
                offsets_weapon.bRecommendSplashDamage = propertyOffset(cls, "bRecommendSplashDamage");
                offsets_weapon.bRecommendAltSplashDamage = propertyOffset(cls, "bRecommendAltSplashDamage");
                offsets_weapon.bWeaponStay = propertyOffset(cls, "bWeaponStay");
                offsets_weapon.bOwnsCrosshair = propertyOffset(cls, "bOwnsCrosshair");
                offsets_weapon.bHideWeapon = propertyOffset(cls, "bHideWeapon");
                offsets_weapon.bMeleeWeapon = propertyOffset(cls, "bMeleeWeapon");
                offsets_weapon.bRapidFire = propertyOffset(cls, "bRapidFire");
                offsets_weapon.bSpecialIcon = propertyOffset(cls, "bSpecialIcon");
                offsets_weapon.FiringSpeed = propertyOffset(cls, "FiringSpeed");
                offsets_weapon.FireOffset = propertyOffset(cls, "FireOffset");
                offsets_weapon.ProjectileClass = propertyOffset(cls, "ProjectileClass");
                offsets_weapon.AltProjectileClass = propertyOffset(cls, "AltProjectileClass");
                offsets_weapon.MyDamageType = propertyOffset(cls, "MyDamageType");
                offsets_weapon.AltDamageType = propertyOffset(cls, "AltDamageType");
                offsets_weapon.ProjectileSpeed = propertyOffset(cls, "ProjectileSpeed");
                offsets_weapon.AltProjectileSpeed = propertyOffset(cls, "AltProjectileSpeed");
                offsets_weapon.aimerror = propertyOffset(cls, "aimerror");
                offsets_weapon.shakemag = propertyOffset(cls, "shakemag");
                offsets_weapon.shaketime = propertyOffset(cls, "shaketime");
                offsets_weapon.shakevert = propertyOffset(cls, "shakevert");
                offsets_weapon.AIRating = propertyOffset(cls, "AIRating");
                offsets_weapon.RefireRate = propertyOffset(cls, "RefireRate");
                offsets_weapon.AltRefireRate = propertyOffset(cls, "AltRefireRate");
                offsets_weapon.FireSound = propertyOffset(cls, "FireSound");
                offsets_weapon.AltFireSound = propertyOffset(cls, "AltFireSound");
                offsets_weapon.CockingSound = propertyOffset(cls, "CockingSound");
                offsets_weapon.SelectSound = propertyOffset(cls, "SelectSound");
                offsets_weapon.Misc1Sound = propertyOffset(cls, "Misc1Sound");
                offsets_weapon.Misc2Sound = propertyOffset(cls, "Misc2Sound");
                offsets_weapon.Misc3Sound = propertyOffset(cls, "Misc3Sound");
                offsets_weapon.MessageNoAmmo = propertyOffset(cls, "MessageNoAmmo");
                offsets_weapon.DeathMessage = propertyOffset(cls, "DeathMessage");
                offsets_weapon.NameColor = propertyOffset(cls, "NameColor");
                offsets_weapon.AdjustedAim = propertyOffset(cls, "AdjustedAim");
                offsets_weapon.bSetFlashTime = propertyOffset(cls, "bSetFlashTime");
                offsets_weapon.bDrawMuzzleFlash = propertyOffset(cls, "bDrawMuzzleFlash");
                offsets_weapon.bMuzzleFlash = propertyOffset(cls, "bMuzzleFlash");
                offsets_weapon.FlashTime = propertyOffset(cls, "FlashTime");
                offsets_weapon.MuzzleScale = propertyOffset(cls, "MuzzleScale");
                offsets_weapon.FlashY = propertyOffset(cls, "FlashY");
                offsets_weapon.FlashO = propertyOffset(cls, "FlashO");
                offsets_weapon.FlashC = propertyOffset(cls, "FlashC");
                offsets_weapon.FlashLength = propertyOffset(cls, "FlashLength");
                offsets_weapon.FlashS = propertyOffset(cls, "FlashS");
                offsets_weapon.MFTexture = propertyOffset(cls, "MFTexture");
                offsets_weapon.MuzzleFlare = propertyOffset(cls, "MuzzleFlare");
                offsets_weapon.FlareOffset = propertyOffset(cls, "FlareOffset");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Projectile");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Projectile\n");
            } else {
                offsets_projectile.speed = propertyOffset(cls, "speed");
                offsets_projectile.MaxSpeed = propertyOffset(cls, "MaxSpeed");
                offsets_projectile.Damage = propertyOffset(cls, "Damage");
                offsets_projectile.MomentumTransfer = propertyOffset(cls, "MomentumTransfer");
                offsets_projectile.MyDamageType = propertyOffset(cls, "MyDamageType");
                offsets_projectile.SpawnSound = propertyOffset(cls, "SpawnSound");
                offsets_projectile.ImpactSound = propertyOffset(cls, "ImpactSound");
                offsets_projectile.MiscSound = propertyOffset(cls, "MiscSound");
                offsets_projectile.ExploWallOut = propertyOffset(cls, "ExploWallOut");
                offsets_projectile.ExplosionDecal = propertyOffset(cls, "ExplosionDecal");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Decoration");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Decoration\n");
            } else {
                offsets_decoration.EffectWhenDestroyed = propertyOffset(cls, "EffectWhenDestroyed");
                offsets_decoration.bPushable = propertyOffset(cls, "bPushable");
                offsets_decoration.bOnlyTriggerable = propertyOffset(cls, "bOnlyTriggerable");
                offsets_decoration.bSplash = propertyOffset(cls, "bSplash");
                offsets_decoration.bBobbing = propertyOffset(cls, "bBobbing");
                offsets_decoration.bWasCarried = propertyOffset(cls, "bWasCarried");
                offsets_decoration.PushSound = propertyOffset(cls, "PushSound");
                offsets_decoration.numLandings = propertyOffset(cls, "numLandings");
                offsets_decoration.contents = propertyOffset(cls, "contents");
                offsets_decoration.content2 = propertyOffset(cls, "content2");
                offsets_decoration.content3 = propertyOffset(cls, "content3");
                offsets_decoration.EndPushSound = propertyOffset(cls, "EndPushSound");
                offsets_decoration.bPushSoundPlaying = propertyOffset(cls, "bPushSoundPlaying");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Canvas");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Canvas\n");
            } else {
                offsets_canvas.Font = propertyOffset(cls, "Font");
                offsets_canvas.SpaceX = propertyOffset(cls, "SpaceX");
                offsets_canvas.SpaceY = propertyOffset(cls, "SpaceY");
                offsets_canvas.OrgX = propertyOffset(cls, "OrgX");
                offsets_canvas.OrgY = propertyOffset(cls, "OrgY");
                offsets_canvas.ClipX = propertyOffset(cls, "ClipX");
                offsets_canvas.ClipY = propertyOffset(cls, "ClipY");
                offsets_canvas.CurX = propertyOffset(cls, "CurX");
                offsets_canvas.CurY = propertyOffset(cls, "CurY");
                offsets_canvas.Z = propertyOffset(cls, "Z");
                offsets_canvas.Style = propertyOffset(cls, "Style");
                offsets_canvas.CurYL = propertyOffset(cls, "CurYL");
                offsets_canvas.DrawColor = propertyOffset(cls, "DrawColor");
                offsets_canvas.bCenter = propertyOffset(cls, "bCenter");
                offsets_canvas.bNoSmooth = propertyOffset(cls, "bNoSmooth");
                offsets_canvas.SizeX = propertyOffset(cls, "SizeX");
                offsets_canvas.SizeY = propertyOffset(cls, "SizeY");
                offsets_canvas.SmallFont = propertyOffset(cls, "SmallFont");
                offsets_canvas.MedFont = propertyOffset(cls, "MedFont");
                offsets_canvas.BigFont = propertyOffset(cls, "BigFont");
                offsets_canvas.LargeFont = propertyOffset(cls, "LargeFont");
                offsets_canvas.Viewport = propertyOffset(cls, "Viewport");
                offsets_canvas.FramePtr = propertyOffset(cls, "FramePtr");
                offsets_canvas.RenderPtr = propertyOffset(cls, "RenderPtr");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Console");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Console\n");
            } else {
                offsets_console.vtblOut = propertyOffset(cls, "vtblOut");
                offsets_console.Viewport = propertyOffset(cls, "Viewport");
                offsets_console.HistoryTop = propertyOffset(cls, "HistoryTop");
                offsets_console.HistoryBot = propertyOffset(cls, "HistoryBot");
                offsets_console.HistoryCur = propertyOffset(cls, "HistoryCur");
                offsets_console.TypedStr = propertyOffset(cls, "TypedStr");
                offsets_console.History = propertyOffset(cls, "History");
                offsets_console.Scrollback = propertyOffset(cls, "Scrollback");
                offsets_console.numLines = propertyOffset(cls, "numLines");
                offsets_console.TopLine = propertyOffset(cls, "TopLine");
                offsets_console.TextLines = propertyOffset(cls, "TextLines");
                offsets_console.MsgTime = propertyOffset(cls, "MsgTime");
                offsets_console.MsgTickTime = propertyOffset(cls, "MsgTickTime");
                offsets_console.MsgText = propertyOffset(cls, "MsgText");
                offsets_console.MsgType = propertyOffset(cls, "MsgType");
                offsets_console.MsgPlayer = propertyOffset(cls, "MsgPlayer");
                offsets_console.MsgTick = propertyOffset(cls, "MsgTick");
                offsets_console.BorderSize = propertyOffset(cls, "BorderSize");
                offsets_console.ConsoleLines = propertyOffset(cls, "ConsoleLines");
                offsets_console.BorderLines = propertyOffset(cls, "BorderLines");
                offsets_console.BorderPixels = propertyOffset(cls, "BorderPixels");
                offsets_console.ConsolePos = propertyOffset(cls, "ConsolePos");
                offsets_console.ConsoleDest = propertyOffset(cls, "ConsoleDest");
                offsets_console.FrameX = propertyOffset(cls, "FrameX");
                offsets_console.FrameY = propertyOffset(cls, "FrameY");
                offsets_console.ConBackground = propertyOffset(cls, "ConBackground");
                offsets_console.Border = propertyOffset(cls, "Border");
                offsets_console.bNoStuff = propertyOffset(cls, "bNoStuff");
                offsets_console.bTyping = propertyOffset(cls, "bTyping");
                offsets_console.bNoDrawWorld = propertyOffset(cls, "bNoDrawWorld");
                offsets_console.bTimeDemo = propertyOffset(cls, "bTimeDemo");
                offsets_console.bStartTimeDemo = propertyOffset(cls, "bStartTimeDemo");
                offsets_console.bRestartTimeDemo = propertyOffset(cls, "bRestartTimeDemo");
                offsets_console.bSaveTimeDemoToFile = propertyOffset(cls, "bSaveTimeDemoToFile");
                offsets_console.StartTime = propertyOffset(cls, "StartTime");
                offsets_console.ExtraTime = propertyOffset(cls, "ExtraTime");
                offsets_console.LastFrameTime = propertyOffset(cls, "LastFrameTime");
                offsets_console.LastSecondStartTime = propertyOffset(cls, "LastSecondStartTime");
                offsets_console.FrameCount = propertyOffset(cls, "FrameCount");
                offsets_console.LastSecondFrameCount = propertyOffset(cls, "LastSecondFrameCount");
                offsets_console.MinFPS = propertyOffset(cls, "MinFPS");
                offsets_console.MaxFPS = propertyOffset(cls, "MaxFPS");
                offsets_console.LastSecFPS = propertyOffset(cls, "LastSecFPS");
                offsets_console.TimeDemoFont = propertyOffset(cls, "TimeDemoFont");
                offsets_console.LoadingMessage = propertyOffset(cls, "LoadingMessage");
                offsets_console.SavingMessage = propertyOffset(cls, "SavingMessage");
                offsets_console.ConnectingMessage = propertyOffset(cls, "ConnectingMessage");
                offsets_console.PausedMessage = propertyOffset(cls, "PausedMessage");
                offsets_console.PrecachingMessage = propertyOffset(cls, "PrecachingMessage");
                offsets_console.FrameRateText = propertyOffset(cls, "FrameRateText");
                offsets_console.AvgText = propertyOffset(cls, "AvgText");
                offsets_console.LastSecText = propertyOffset(cls, "LastSecText");
                offsets_console.MinText = propertyOffset(cls, "MinText");
                offsets_console.MaxText = propertyOffset(cls, "MaxText");
                offsets_console.fpsText = propertyOffset(cls, "fpsText");
                offsets_console.SecondsText = propertyOffset(cls, "SecondsText");
                offsets_console.FramesText = propertyOffset(cls, "FramesText");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.HUD");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.HUD\n");
            } else {
                offsets_hud.HudMode = propertyOffset(cls, "HudMode");
                offsets_hud.Crosshair = propertyOffset(cls, "Crosshair");
                offsets_hud.MainMenuType = propertyOffset(cls, "MainMenuType");
                offsets_hud.HUDConfigWindowType = propertyOffset(cls, "HUDConfigWindowType");
                offsets_hud.WhiteColor = propertyOffset(cls, "WhiteColor");
                offsets_hud.MainMenu = propertyOffset(cls, "MainMenu");
                offsets_hud.HUDMutator = propertyOffset(cls, "HUDMutator");
                offsets_hud.PlayerOwner = propertyOffset(cls, "PlayerOwner");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Bitmap");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Bitmap\n");
            } else {
                offsets_bitmap.Format = propertyOffset(cls, "Format");
                offsets_bitmap.Palette = propertyOffset(cls, "Palette");
                offsets_bitmap.UBits = propertyOffset(cls, "UBits");
                offsets_bitmap.VBits = propertyOffset(cls, "VBits");
                offsets_bitmap.USize = propertyOffset(cls, "USize");
                offsets_bitmap.VSize = propertyOffset(cls, "VSize");
                offsets_bitmap.UClamp = propertyOffset(cls, "UClamp");
                offsets_bitmap.VClamp = propertyOffset(cls, "VClamp");
                offsets_bitmap.MipZero = propertyOffset(cls, "MipZero");
                offsets_bitmap.MaxColor = propertyOffset(cls, "MaxColor");
                offsets_bitmap.InternalTime = propertyOffset(cls, "InternalTime");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Texture");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Texture\n");
            } else {
                offsets_texture.BumpMap = propertyOffset(cls, "BumpMap");
                offsets_texture.DetailTexture = propertyOffset(cls, "DetailTexture");
                offsets_texture.MacroTexture = propertyOffset(cls, "MacroTexture");
                offsets_texture.Diffuse = propertyOffset(cls, "Diffuse");
                offsets_texture.Specular = propertyOffset(cls, "Specular");
                offsets_texture.Alpha = propertyOffset(cls, "Alpha");
                offsets_texture.DrawScale = propertyOffset(cls, "DrawScale");
                offsets_texture.Friction = propertyOffset(cls, "Friction");
                offsets_texture.MipMult = propertyOffset(cls, "MipMult");
                offsets_texture.FootstepSound = propertyOffset(cls, "FootstepSound");
                offsets_texture.HitSound = propertyOffset(cls, "HitSound");
                offsets_texture.bInvisible = propertyOffset(cls, "bInvisible");
                offsets_texture.bMasked = propertyOffset(cls, "bMasked");
                offsets_texture.bTransparent = propertyOffset(cls, "bTransparent");
                offsets_texture.bNotSolid = propertyOffset(cls, "bNotSolid");
                offsets_texture.bEnvironment = propertyOffset(cls, "bEnvironment");
                offsets_texture.bSemisolid = propertyOffset(cls, "bSemisolid");
                offsets_texture.bModulate = propertyOffset(cls, "bModulate");
                offsets_texture.bFakeBackdrop = propertyOffset(cls, "bFakeBackdrop");
                offsets_texture.bTwoSided = propertyOffset(cls, "bTwoSided");
                offsets_texture.bAutoUPan = propertyOffset(cls, "bAutoUPan");
                offsets_texture.bAutoVPan = propertyOffset(cls, "bAutoVPan");
                offsets_texture.bNoSmooth = propertyOffset(cls, "bNoSmooth");
                offsets_texture.bBigWavy = propertyOffset(cls, "bBigWavy");
                offsets_texture.bSmallWavy = propertyOffset(cls, "bSmallWavy");
                offsets_texture.bWaterWavy = propertyOffset(cls, "bWaterWavy");
                offsets_texture.bLowShadowDetail = propertyOffset(cls, "bLowShadowDetail");
                offsets_texture.bNoMerge = propertyOffset(cls, "bNoMerge");
                offsets_texture.bCloudWavy = propertyOffset(cls, "bCloudWavy");
                offsets_texture.bDirtyShadows = propertyOffset(cls, "bDirtyShadows");
                offsets_texture.bHighLedge = propertyOffset(cls, "bHighLedge");
                offsets_texture.bSpecialLit = propertyOffset(cls, "bSpecialLit");
                offsets_texture.bGouraud = propertyOffset(cls, "bGouraud");
                offsets_texture.bUnlit = propertyOffset(cls, "bUnlit");
                offsets_texture.bHighShadowDetail = propertyOffset(cls, "bHighShadowDetail");
                offsets_texture.bPortal = propertyOffset(cls, "bPortal");
                offsets_texture.bMirrored = propertyOffset(cls, "bMirrored");
                offsets_texture.bX2 = propertyOffset(cls, "bX2");
                offsets_texture.bX3 = propertyOffset(cls, "bX3");
                offsets_texture.bX4 = propertyOffset(cls, "bX4");
                offsets_texture.bX5 = propertyOffset(cls, "bX5");
                offsets_texture.bX6 = propertyOffset(cls, "bX6");
                offsets_texture.bX7 = propertyOffset(cls, "bX7");
                offsets_texture.bHighColorQuality = propertyOffset(cls, "bHighColorQuality");
                offsets_texture.bHighTextureQuality = propertyOffset(cls, "bHighTextureQuality");
                offsets_texture.bRealtime = propertyOffset(cls, "bRealtime");
                offsets_texture.bParametric = propertyOffset(cls, "bParametric");
                offsets_texture.bRealtimeChanged = propertyOffset(cls, "bRealtimeChanged");
                offsets_texture.bHasComp = propertyOffset(cls, "bHasComp");
                offsets_texture.LODSet = propertyOffset(cls, "LODSet");
                offsets_texture.AnimNext = propertyOffset(cls, "AnimNext");
                offsets_texture.AnimCurrent = propertyOffset(cls, "AnimCurrent");
                offsets_texture.PrimeCount = propertyOffset(cls, "PrimeCount");
                offsets_texture.PrimeCurrent = propertyOffset(cls, "PrimeCurrent");
                offsets_texture.MinFrameRate = propertyOffset(cls, "MinFrameRate");
                offsets_texture.MaxFrameRate = propertyOffset(cls, "MaxFrameRate");
                offsets_texture.Accumulator = propertyOffset(cls, "Accumulator");
                offsets_texture.Mips = propertyOffset(cls, "Mips");
                offsets_texture.CompMips = propertyOffset(cls, "CompMips");
                offsets_texture.CompFormat = propertyOffset(cls, "CompFormat");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Player");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Player\n");
            } else {
                offsets_player.vfOut = propertyOffset(cls, "vfOut");
                offsets_player.vfExec = propertyOffset(cls, "vfExec");
                offsets_player.Actor = propertyOffset(cls, "Actor");
                offsets_player.Console = propertyOffset(cls, "Console");
                offsets_player.bWindowsMouseAvailable = propertyOffset(cls, "bWindowsMouseAvailable");
                offsets_player.bShowWindowsMouse = propertyOffset(cls, "bShowWindowsMouse");
                offsets_player.bSuspendPrecaching = propertyOffset(cls, "bSuspendPrecaching");
                offsets_player.WindowsMouseX = propertyOffset(cls, "WindowsMouseX");
                offsets_player.WindowsMouseY = propertyOffset(cls, "WindowsMouseY");
                offsets_player.CurrentNetSpeed = propertyOffset(cls, "CurrentNetSpeed");
                offsets_player.ConfiguredInternetSpeed = propertyOffset(cls, "ConfiguredInternetSpeed");
                offsets_player.ConfiguredLanSpeed = propertyOffset(cls, "ConfiguredLanSpeed");
                offsets_player.SelectedCursor = propertyOffset(cls, "SelectedCursor");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Light");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Light\n");
            } else {
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Info");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Info\n");
            } else {
            }
        }
        {
            UClass* cls = objects::findClass("Engine.PlayerReplicationInfo");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.PlayerReplicationInfo\n");
            } else {
                offsets_playerreplicationinfo.PlayerName = propertyOffset(cls, "PlayerName");
                offsets_playerreplicationinfo.OldName = propertyOffset(cls, "OldName");
                offsets_playerreplicationinfo.PlayerID = propertyOffset(cls, "PlayerID");
                offsets_playerreplicationinfo.TeamName = propertyOffset(cls, "TeamName");
                offsets_playerreplicationinfo.Team = propertyOffset(cls, "Team");
                offsets_playerreplicationinfo.TeamID = propertyOffset(cls, "TeamID");
                offsets_playerreplicationinfo.Score = propertyOffset(cls, "Score");
                offsets_playerreplicationinfo.Deaths = propertyOffset(cls, "Deaths");
                offsets_playerreplicationinfo.VoiceType = propertyOffset(cls, "VoiceType");
                offsets_playerreplicationinfo.HasFlag = propertyOffset(cls, "HasFlag");
                offsets_playerreplicationinfo.Ping = propertyOffset(cls, "Ping");
                offsets_playerreplicationinfo.PacketLoss = propertyOffset(cls, "PacketLoss");
                offsets_playerreplicationinfo.bIsFemale = propertyOffset(cls, "bIsFemale");
                offsets_playerreplicationinfo.bIsABot = propertyOffset(cls, "bIsABot");
                offsets_playerreplicationinfo.bFeigningDeath = propertyOffset(cls, "bFeigningDeath");
                offsets_playerreplicationinfo.bIsSpectator = propertyOffset(cls, "bIsSpectator");
                offsets_playerreplicationinfo.bWaitingPlayer = propertyOffset(cls, "bWaitingPlayer");
                offsets_playerreplicationinfo.bAdmin = propertyOffset(cls, "bAdmin");
                offsets_playerreplicationinfo.TalkTexture = propertyOffset(cls, "TalkTexture");
                offsets_playerreplicationinfo.PlayerZone = propertyOffset(cls, "PlayerZone");
                offsets_playerreplicationinfo.PlayerLocation = propertyOffset(cls, "PlayerLocation");
                offsets_playerreplicationinfo.StartTime = propertyOffset(cls, "StartTime");
                offsets_playerreplicationinfo.TimeAcc = propertyOffset(cls, "TimeAcc");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Decal");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Decal\n");
            } else {
                offsets_decal.MultiDecalLevel = propertyOffset(cls, "MultiDecalLevel");
                offsets_decal.LastRenderedTime = propertyOffset(cls, "LastRenderedTime");
                offsets_decal.SurfList = propertyOffset(cls, "SurfList");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Teleporter");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Teleporter\n");
            } else {
                offsets_teleporter.URL = propertyOffset(cls, "URL");
                offsets_teleporter.ProductRequired = propertyOffset(cls, "ProductRequired");
                offsets_teleporter.bChangesVelocity = propertyOffset(cls, "bChangesVelocity");
                offsets_teleporter.bChangesYaw = propertyOffset(cls, "bChangesYaw");
                offsets_teleporter.bReversesX = propertyOffset(cls, "bReversesX");
                offsets_teleporter.bReversesY = propertyOffset(cls, "bReversesY");
                offsets_teleporter.bReversesZ = propertyOffset(cls, "bReversesZ");
                offsets_teleporter.bEnabled = propertyOffset(cls, "bEnabled");
                offsets_teleporter.TargetVelocity = propertyOffset(cls, "TargetVelocity");
                offsets_teleporter.TriggerActor = propertyOffset(cls, "TriggerActor");
                offsets_teleporter.TriggerActor2 = propertyOffset(cls, "TriggerActor2");
                offsets_teleporter.LastFired = propertyOffset(cls, "LastFired");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.ScriptedTexture");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.ScriptedTexture\n");
            } else {
                offsets_scriptedtexture.NotifyActor = propertyOffset(cls, "NotifyActor");
                offsets_scriptedtexture.SourceTexture = propertyOffset(cls, "SourceTexture");
                offsets_scriptedtexture.Junk1 = propertyOffset(cls, "Junk1");
                offsets_scriptedtexture.Junk2 = propertyOffset(cls, "Junk2");
                offsets_scriptedtexture.Junk3 = propertyOffset(cls, "Junk3");
                offsets_scriptedtexture.LocalTime = propertyOffset(cls, "LocalTime");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Carcass");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Carcass\n");
            } else {
                offsets_carcass.bPlayerCarcass = propertyOffset(cls, "bPlayerCarcass");
                offsets_carcass.flies = propertyOffset(cls, "flies");
                offsets_carcass.rats = propertyOffset(cls, "rats");
                offsets_carcass.bReducedHeight = propertyOffset(cls, "bReducedHeight");
                offsets_carcass.bDecorative = propertyOffset(cls, "bDecorative");
                offsets_carcass.bSlidingCarcass = propertyOffset(cls, "bSlidingCarcass");
                offsets_carcass.CumulativeDamage = propertyOffset(cls, "CumulativeDamage");
                offsets_carcass.PlayerOwner = propertyOffset(cls, "PlayerOwner");
                offsets_carcass.Bugs = propertyOffset(cls, "Bugs");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Ammo");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Ammo\n");
            } else {
                offsets_ammo.AmmoAmount = propertyOffset(cls, "AmmoAmount");
                offsets_ammo.MaxAmmo = propertyOffset(cls, "MaxAmmo");
                offsets_ammo.ParentAmmo = propertyOffset(cls, "ParentAmmo");
                offsets_ammo.UsedInWeaponSlot = propertyOffset(cls, "UsedInWeaponSlot");
                offsets_ammo.PAmmo = propertyOffset(cls, "PAmmo");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Pickup");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Pickup\n");
            } else {
                offsets_pickup.Inv = propertyOffset(cls, "Inv");
                offsets_pickup.NumCopies = propertyOffset(cls, "NumCopies");
                offsets_pickup.bCanHaveMultipleCopies = propertyOffset(cls, "bCanHaveMultipleCopies");
                offsets_pickup.bCanActivate = propertyOffset(cls, "bCanActivate");
                offsets_pickup.ExpireMessage = propertyOffset(cls, "ExpireMessage");
                offsets_pickup.bAutoActivate = propertyOffset(cls, "bAutoActivate");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.PathNode");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.PathNode\n");
            } else {
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Trigger");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Trigger\n");
            } else {
                offsets_trigger.TriggerType = propertyOffset(cls, "TriggerType");
                offsets_trigger.Message = propertyOffset(cls, "Message");
                offsets_trigger.bTriggerOnceOnly = propertyOffset(cls, "bTriggerOnceOnly");
                offsets_trigger.bInitiallyActive = propertyOffset(cls, "bInitiallyActive");
                offsets_trigger.ClassProximityType = propertyOffset(cls, "ClassProximityType");
                offsets_trigger.RepeatTriggerTime = propertyOffset(cls, "RepeatTriggerTime");
                offsets_trigger.ReTriggerDelay = propertyOffset(cls, "ReTriggerDelay");
                offsets_trigger.TriggerTime = propertyOffset(cls, "TriggerTime");
                offsets_trigger.DamageThreshold = propertyOffset(cls, "DamageThreshold");
                offsets_trigger.TriggerActor = propertyOffset(cls, "TriggerActor");
                offsets_trigger.TriggerActor2 = propertyOffset(cls, "TriggerActor2");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Effects");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Effects\n");
            } else {
                offsets_effects.EffectSound1 = propertyOffset(cls, "EffectSound1");
                offsets_effects.EffectSound2 = propertyOffset(cls, "EffectSound2");
                offsets_effects.bOnlyTriggerable = propertyOffset(cls, "bOnlyTriggerable");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.SpawnNotify");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.SpawnNotify\n");
            } else {
                offsets_spawnnotify.ActorClass = propertyOffset(cls, "ActorClass");
                offsets_spawnnotify.Next = propertyOffset(cls, "Next");
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Keypoint");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Keypoint\n");
            } else {
            }
        }
        {
            UClass* cls = objects::findClass("Engine.Mesh");
            if (!cls) {
                std::fprintf(stderr, "missing class Engine.Mesh\n");
            } else {
            }
        }
    }
}
