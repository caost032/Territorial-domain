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
typedef _ApiVersionDart = int Function();
typedef _AbiQueryNative = Int32 Function(
  Uint32,
  Pointer<OdgFfiAbiInfo>,
  Uint64,
  Pointer<Uint64>,
);
typedef _AbiQueryDart = int Function(
  int,
  Pointer<OdgFfiAbiInfo>,
  int,
  Pointer<Uint64>,
);
typedef _InitNative = Int32 Function(Uint64, Uint32, Uint32);
typedef _InitDart = int Function(int, int, int);
typedef _ResizeNative = Int32 Function(Uint32, Uint32);
typedef _ResizeDart = int Function(int, int);
typedef _ResetNative = Void Function(Uint64);
typedef _ResetDart = void Function(int);
typedef _SetInputNative = Void Function(
  Int32,
  Int32,
  Int32,
  Int32,
  Uint32,
);
typedef _SetInputDart = void Function(int, int, int, int, int);
typedef _TickNative = Void Function(Uint32);
typedef _TickDart = void Function(int);
typedef _RenderNative = UintPtr Function();
typedef _RenderDart = int Function();
typedef _CopyFrameNative = Int32 Function(
  Pointer<Uint8>,
  Uint64,
  Pointer<Uint64>,
);
typedef _CopyFrameDart = int Function(Pointer<Uint8>, int, Pointer<Uint64>);
typedef _CopyStatsNative = Int32 Function(
  Pointer<OdgGameStats>,
  Uint64,
  Pointer<Uint64>,
);
typedef _CopyStatsDart = int Function(
  Pointer<OdgGameStats>,
  int,
  Pointer<Uint64>,
);
typedef _SetU32Native = Void Function(Uint32);
typedef _SetU32Dart = void Function(int);
typedef _U32Native = Uint32 Function();
typedef _U32Dart = int Function();
typedef _U64Native = Uint64 Function();
typedef _U64Dart = int Function();
typedef _RankNative = Uint32 Function(Uint32);
typedef _RankDart = int Function(int);

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
      : apiVersion =
            library.lookupFunction<_ApiVersionNative, _ApiVersionDart>(
          'odg_api_version',
        ),
        ffiAbiQuery = library.lookupFunction<_AbiQueryNative, _AbiQueryDart>(
          'odg_ffi_abi_query',
        ),
        init = library.lookupFunction<_InitNative, _InitDart>('odg_init'),
        resize =
            library.lookupFunction<_ResizeNative, _ResizeDart>('odg_resize'),
        reset = library.lookupFunction<_ResetNative, _ResetDart>('odg_reset'),
        setInput = library.lookupFunction<_SetInputNative, _SetInputDart>(
          'odg_set_input',
        ),
        tickUs =
            library.lookupFunction<_TickNative, _TickDart>('odg_tick_us'),
        renderFrame =
            library.lookupFunction<_RenderNative, _RenderDart>(
          'odg_render_frame',
        ),
        copyFramebuffer =
            library.lookupFunction<_CopyFrameNative, _CopyFrameDart>(
          'odg_copy_framebuffer',
        ),
        copyStats =
            library.lookupFunction<_CopyStatsNative, _CopyStatsDart>(
          'odg_copy_stats',
        ),
        framebufferBytes = library.lookupFunction<_U32Native, _U32Dart>(
          'odg_framebuffer_bytes',
        ),
        framebufferStrideBytes =
            library.lookupFunction<_U32Native, _U32Dart>(
          'odg_framebuffer_stride_bytes',
        ),
        renderWidth = library.lookupFunction<_U32Native, _U32Dart>(
          'odg_render_width',
        ),
        renderHeight = library.lookupFunction<_U32Native, _U32Dart>(
          'odg_render_height',
        ),
        setVisualTheme =
            library.lookupFunction<_SetU32Native, _SetU32Dart>(
          'odg_set_visual_theme',
        ),
        visualTheme = library.lookupFunction<_U32Native, _U32Dart>(
          'odg_visual_theme',
        ),
        setPresentationMode =
            library.lookupFunction<_SetU32Native, _SetU32Dart>(
          'odg_set_presentation_mode',
        ),
        stateHash = library.lookupFunction<_U64Native, _U64Dart>(
          'odg_state_hash',
        ),
        leaderCount = library.lookupFunction<_U32Native, _U32Dart>(
          'odg_leader_count',
        ),
        leaderScore = library.lookupFunction<_RankNative, _RankDart>(
          'odg_leader_score',
        ),
        leaderNameCode = library.lookupFunction<_RankNative, _RankDart>(
          'odg_leader_name_code',
        ),
        leaderIsPlayer = library.lookupFunction<_RankNative, _RankDart>(
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
  final _ApiVersionDart apiVersion;
  final _AbiQueryDart ffiAbiQuery;
  final _InitDart init;
  final _ResizeDart resize;
  final _ResetDart reset;
  final _SetInputDart setInput;
  final _TickDart tickUs;
  final _RenderDart renderFrame;
  final _CopyFrameDart copyFramebuffer;
  final _CopyStatsDart copyStats;
  final _U32Dart framebufferBytes;
  final _U32Dart framebufferStrideBytes;
  final _U32Dart renderWidth;
  final _U32Dart renderHeight;
  final _SetU32Dart setVisualTheme;
  final _U32Dart visualTheme;
  final _SetU32Dart setPresentationMode;
  final _U64Dart stateHash;
  final _U32Dart leaderCount;
  final _RankDart leaderScore;
  final _RankDart leaderNameCode;
  final _RankDart leaderIsPlayer;

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
      const int requiredFeatures = odgFfiFeatureFramebufferCopy |
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
