import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';

const int odgApiVersion = 14;
const int odgFfiAbiVersion = 1;
const int odgFfiEndianMarker = 0x01020304;
const int odgPixelFormatRgba8 = 1;
const int odgTickRate = 120;

const int odgStatusOk = 0;
const int odgStatusInvalidArgument = 1;
const int odgStatusInvalidState = 2;
const int odgStatusUnsupported = 3;
const int odgStatusBufferTooSmall = 4;
const int odgStatusVersionMismatch = 5;

const int odgFfiFeatureFramebufferPointer = 1 << 0;
const int odgFfiFeatureFramebufferCopy = 1 << 1;
const int odgFfiFeatureStatsPointer = 1 << 2;
const int odgFfiFeatureStatsCopy = 1 << 3;
const int odgFfiFeaturePortraitRender = 1 << 4;
const int odgFfiFeatureFixed120Hz = 1 << 5;
const int odgFfiFeatureCameraInput = 1 << 6;

const int odgVisualThemeNeonTides = 0;
const int odgVisualThemeEmeraldCrown = 1;
const int odgVisualThemeSolarEmber = 2;
const int odgVisualThemeObsidianPulse = 3;
const int odgVisualThemeCount = 4;

const int odgPresentationGameplay = 0;
const int odgPresentationShowcase = 1;

const int odgButtonFire = 1 << 0;
const int odgButtonDash = 1 << 1;
const int odgButtonRestart = 1 << 2;
const int odgButtonAction = 1 << 3;
const int odgButtonDrop = 1 << 4;

final class OdgFfiAbiInfo extends Struct {
  @Uint32()
  external int structSize;

  @Uint32()
  external int ffiAbiVersion;

  @Uint32()
  external int engineApiVersion;

  @Uint32()
  external int endianMarker;

  @Uint32()
  external int gameStatsSize;

  @Uint32()
  external int leaderEntrySize;

  @Uint32()
  external int tickRate;

  @Uint32()
  external int maxRenderWidth;

  @Uint32()
  external int maxRenderHeight;

  @Uint32()
  external int maxRenderPixels;

  @Uint32()
  external int framebufferPixelFormat;

  @Uint32()
  external int framebufferBytesPerPixel;

  @Uint64()
  external int featureBits;

  @Uint64()
  external int reservedU64;
}

final class OdgGameStats extends Struct {
  @Uint32()
  external int structSize;
  @Uint32()
  external int apiVersion;
  @Uint64()
  external int tick;
  @Uint64()
  external int matchSeed;
  @Uint32()
  external int width;
  @Uint32()
  external int height;
  @Uint32()
  external int aliveCount;
  @Uint32()
  external int playerAlive;
  @Uint32()
  external int playerHealth;
  @Uint32()
  external int playerMaxHealth;
  @Uint32()
  external int playerLevel;
  @Uint32()
  external int playerScore;
  @Uint32()
  external int playerKills;
  @Uint32()
  external int playerDeaths;
  @Uint32()
  external int zoneRadiusMilli;
  @Uint32()
  external int simulationHz;
  @Uint32()
  external int renderTriangles;
  @Uint32()
  external int renderPixelsTouched;
  @Uint64()
  external int deterministicStateHash;
  @Uint32()
  external int territoryCells;
  @Uint32()
  external int territoryTotalCells;
  @Uint32()
  external int territoryPermille;
  @Uint32()
  external int playerTrailCells;
  @Uint32()
  external int playerTrailActive;
  @Uint32()
  external int matchOver;
  @Uint32()
  external int winnerId;
  @Uint32()
  external int playerDeathReason;
  @Uint32()
  external int turretTotal;
  @Uint32()
  external int playerOwnedTurrets;
  @Uint32()
  external int playerCarryingTurret;
  @Uint32()
  external int carriedTurretAmmo;
  @Uint32()
  external int turretActionAvailable;
  @Uint32()
  external int ammoCratesTotal;
  @Uint32()
  external int playerCarryingAmmoCrate;
  @Uint32()
  external int playerCarriedAmmo;
  @Uint32()
  external int playerAmmoReserve;
  @Uint32()
  external int chipsTotal;
  @Uint32()
  external int playerCarryingChip;
  @Uint32()
  external int playerChipKind;
  @Uint32()
  external int hackActionAvailable;
  @Uint32()
  external int dropActionAvailable;
  @Uint32()
  external int nearbyOwnedTurretVisible;
  @Uint32()
  external int nearbyOwnedTurretAmmo;
  @Uint32()
  external int nearbyOwnedTurretMaxAmmo;
}

final class OdgLeaderEntry extends Struct {
  @Uint32()
  external int actorId;
  @Uint32()
  external int score;
  @Uint32()
  external int level;
  @Uint32()
  external int alive;
  @Uint32()
  external int isPlayer;
  @Uint32()
  external int nameCode;
}

typedef _ApiVersionNative = Uint32 Function();
typedef OdgApiVersionFunction = int Function();
typedef _AbiQueryNative =
    Int32 Function(Uint32, Pointer<OdgFfiAbiInfo>, Uint64, Pointer<Uint64>);
typedef OdgAbiQueryFunction =
    int Function(int, Pointer<OdgFfiAbiInfo>, int, Pointer<Uint64>);
typedef _InitNative = Int32 Function(Uint64, Uint32, Uint32);
typedef OdgInitFunction = int Function(int, int, int);
typedef _ResizeNative = Int32 Function(Uint32, Uint32);
typedef OdgResizeFunction = int Function(int, int);
typedef _ResetNative = Void Function(Uint64);
typedef OdgResetFunction = void Function(int);
typedef _SetInputNative = Void Function(Int32, Int32, Int32, Int32, Uint32);
typedef OdgSetInputFunction = void Function(int, int, int, int, int);
typedef _TickNative = Void Function(Uint32);
typedef OdgTickFunction = void Function(int);
typedef _RenderNative = UintPtr Function();
typedef OdgRenderFunction = int Function();
typedef _CopyFrameNative =
    Int32 Function(Pointer<Uint8>, Uint64, Pointer<Uint64>);
typedef OdgCopyFrameFunction = int Function(
  Pointer<Uint8>,
  int,
  Pointer<Uint64>,
);
typedef _CopyStatsNative =
    Int32 Function(Pointer<OdgGameStats>, Uint64, Pointer<Uint64>);
typedef OdgCopyStatsFunction =
    int Function(Pointer<OdgGameStats>, int, Pointer<Uint64>);
typedef _SetU32Native = Void Function(Uint32);
typedef OdgSetU32Function = void Function(int);
typedef _U32Native = Uint32 Function();
typedef OdgU32Function = int Function();
typedef _U64Native = Uint64 Function();
typedef OdgU64Function = int Function();
typedef _RankNative = Uint32 Function(Uint32);
typedef OdgRankFunction = int Function(int);

final class OdgAbiDescriptor {
  const OdgAbiDescriptor({
    required this.maxRenderWidth,
    required this.maxRenderHeight,
    required this.maxRenderPixels,
    required this.featureBits,
  });

  final int maxRenderWidth;
  final int maxRenderHeight;
  final int maxRenderPixels;
  final int featureBits;
}

final class OdgNativeException implements Exception {
  const OdgNativeException(this.message, [this.status]);

  final String message;
  final int? status;

  @override
  String toString() => status == null
      ? 'OdgNativeException: $message'
      : 'OdgNativeException: $message (status $status)';
}

final class OdgNativeApi {
  OdgNativeApi._(this.library)
    : apiVersion = library.lookupFunction<_ApiVersionNative, OdgApiVersionFunction>(
        'odg_api_version',
      ),
      ffiAbiQuery = library.lookupFunction<_AbiQueryNative, OdgAbiQueryFunction>(
        'odg_ffi_abi_query',
      ),
      init = library.lookupFunction<_InitNative, OdgInitFunction>('odg_init'),
      resize = library.lookupFunction<_ResizeNative, OdgResizeFunction>('odg_resize'),
      reset = library.lookupFunction<_ResetNative, OdgResetFunction>('odg_reset'),
      setInput = library.lookupFunction<_SetInputNative, OdgSetInputFunction>(
        'odg_set_input',
      ),
      tickUs = library.lookupFunction<_TickNative, OdgTickFunction>('odg_tick_us'),
      renderFrame = library.lookupFunction<_RenderNative, OdgRenderFunction>(
        'odg_render_frame',
      ),
      copyFramebuffer = library
          .lookupFunction<_CopyFrameNative, OdgCopyFrameFunction>(
            'odg_copy_framebuffer',
          ),
      copyStats = library.lookupFunction<_CopyStatsNative, OdgCopyStatsFunction>(
        'odg_copy_stats',
      ),
      framebufferBytes = library.lookupFunction<_U32Native, OdgU32Function>(
        'odg_framebuffer_bytes',
      ),
      framebufferStrideBytes = library.lookupFunction<_U32Native, OdgU32Function>(
        'odg_framebuffer_stride_bytes',
      ),
      renderWidth = library.lookupFunction<_U32Native, OdgU32Function>(
        'odg_render_width',
      ),
      renderHeight = library.lookupFunction<_U32Native, OdgU32Function>(
        'odg_render_height',
      ),
      setVisualTheme = library.lookupFunction<_SetU32Native, OdgSetU32Function>(
        'odg_set_visual_theme',
      ),
      visualTheme = library.lookupFunction<_U32Native, OdgU32Function>(
        'odg_visual_theme',
      ),
      setPresentationMode = library.lookupFunction<_SetU32Native, OdgSetU32Function>(
        'odg_set_presentation_mode',
      ),
      stateHash = library.lookupFunction<_U64Native, OdgU64Function>(
        'odg_state_hash',
      ),
      leaderCount = library.lookupFunction<_U32Native, OdgU32Function>(
        'odg_leader_count',
      ),
      leaderScore = library.lookupFunction<_RankNative, OdgRankFunction>(
        'odg_leader_score',
      ),
      leaderNameCode = library.lookupFunction<_RankNative, OdgRankFunction>(
        'odg_leader_name_code',
      ),
      leaderIsPlayer = library.lookupFunction<_RankNative, OdgRankFunction>(
        'odg_leader_is_player',
      );

  factory OdgNativeApi.open() {
    if (!Platform.isAndroid && !Platform.isLinux) {
      throw const OdgNativeException(
        'This Flutter target requires the packaged ODPAR native library.',
      );
    }
    return OdgNativeApi._(
      DynamicLibrary.open('libodpar_territorial_domain.so'),
    );
  }

  final DynamicLibrary library;
  final OdgApiVersionFunction apiVersion;
  final OdgAbiQueryFunction ffiAbiQuery;
  final OdgInitFunction init;
  final OdgResizeFunction resize;
  final OdgResetFunction reset;
  final OdgSetInputFunction setInput;
  final OdgTickFunction tickUs;
  final OdgRenderFunction renderFrame;
  final OdgCopyFrameFunction copyFramebuffer;
  final OdgCopyStatsFunction copyStats;
  final OdgU32Function framebufferBytes;
  final OdgU32Function framebufferStrideBytes;
  final OdgU32Function renderWidth;
  final OdgU32Function renderHeight;
  final OdgSetU32Function setVisualTheme;
  final OdgU32Function visualTheme;
  final OdgSetU32Function setPresentationMode;
  final OdgU64Function stateHash;
  final OdgU32Function leaderCount;
  final OdgRankFunction leaderScore;
  final OdgRankFunction leaderNameCode;
  final OdgRankFunction leaderIsPlayer;

  OdgAbiDescriptor queryAndValidateAbi() {
    final Pointer<OdgFfiAbiInfo> info = calloc<OdgFfiAbiInfo>();
    final Pointer<Uint64> required = calloc<Uint64>();
    try {
      final int status = ffiAbiQuery(
        odgFfiAbiVersion,
        info,
        sizeOf<OdgFfiAbiInfo>(),
        required,
      );
      if (status != odgStatusOk) {
        throw OdgNativeException('FFI ABI query failed', status);
      }
      final OdgFfiAbiInfo value = info.ref;
      const int requiredFeatures =
          odgFfiFeatureFramebufferCopy |
          odgFfiFeatureStatsCopy |
          odgFfiFeaturePortraitRender |
          odgFfiFeatureFixed120Hz |
          odgFfiFeatureCameraInput;
      if (required.value != sizeOf<OdgFfiAbiInfo>() ||
          value.structSize != sizeOf<OdgFfiAbiInfo>() ||
          value.ffiAbiVersion != odgFfiAbiVersion ||
          value.engineApiVersion != odgApiVersion ||
          apiVersion() != odgApiVersion ||
          value.endianMarker != odgFfiEndianMarker ||
          value.gameStatsSize != sizeOf<OdgGameStats>() ||
          value.leaderEntrySize != sizeOf<OdgLeaderEntry>() ||
          value.tickRate != odgTickRate ||
          value.framebufferPixelFormat != odgPixelFormatRgba8 ||
          value.framebufferBytesPerPixel != 4 ||
          (value.featureBits & requiredFeatures) != requiredFeatures ||
          value.maxRenderWidth <= 0 ||
          value.maxRenderHeight <= 0 ||
          value.maxRenderPixels <= 0) {
        throw const OdgNativeException(
          'Native library does not satisfy ODG API 14 / FFI ABI v1.',
        );
      }
      return OdgAbiDescriptor(
        maxRenderWidth: value.maxRenderWidth,
        maxRenderHeight: value.maxRenderHeight,
        maxRenderPixels: value.maxRenderPixels,
        featureBits: value.featureBits,
      );
    } finally {
      calloc.free(required);
      calloc.free(info);
    }
  }
}
