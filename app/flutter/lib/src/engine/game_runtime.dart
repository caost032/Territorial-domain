import 'dart:async';
import 'dart:ffi' hide Size;
import 'dart:math' as math;
import 'dart:ui' as ui;

import 'package:ffi/ffi.dart';
import 'package:flutter/foundation.dart';
import 'package:flutter/scheduler.dart';
import 'package:flutter/widgets.dart';

import '../input/input_router.dart';
import '../native/odg_bindings.dart';
import '../render/raster_budget.dart';
import 'game_snapshot.dart';

final class GameRuntime extends ChangeNotifier {
  GameRuntime._({required this.input, required this.api, required this.abi})
    : _frameCapacity = abi.maxRenderPixels * 4,
      _frameBuffer = calloc<Uint8>(abi.maxRenderPixels * 4),
      _statsBuffer = calloc<OdgGameStats>(),
      _required = calloc<Uint64>() {
    _ticker = Ticker(_onTick);
  }

  factory GameRuntime.open(MultiTouchInputRouter input) {
    final OdgNativeApi api = OdgNativeApi.open();
    final OdgAbiDescriptor abi = api.queryAndValidateAbi();
    final GameRuntime runtime = GameRuntime._(input: input, api: api, abi: abi);
    runtime._initialize();
    return runtime;
  }

  static const List<String> leaderNames = <String>[
    'NOVA',
    'EMBER',
    'ORBIT',
    'KITE',
    'MOSS',
    'VANTA',
    'AURA',
    'HEX',
    'LYNX',
    'ONYX',
  ];

  final MultiTouchInputRouter input;
  final OdgNativeApi api;
  final OdgAbiDescriptor abi;
  final RasterGovernor _governor = RasterGovernor();
  final int _frameCapacity;
  final Pointer<Uint8> _frameBuffer;
  final Pointer<OdgGameStats> _statsBuffer;
  final Pointer<Uint64> _required;
  late final Ticker _ticker;

  ui.Image? _image;
  GameSnapshot _snapshot = const GameSnapshot.empty();
  RasterQuality _quality = RasterQuality.balanced;
  RenderSize _renderSize = const RenderSize(360, 640);
  Size _viewport = const Size(360, 640);
  bool _started = false;
  bool _lifecycleActive = true;
  bool _hostPaused = false;
  bool _decodePending = false;
  bool _disposed = false;
  Duration? _previousElapsed;

  ui.Image? get image => _image;
  GameSnapshot get snapshot => _snapshot;
  RasterQuality get quality => _quality;
  RenderSize get renderSize => _renderSize;
  bool get started => _started;

  void _initialize() {
    _renderSize = RasterBudget.resolve(
      viewportAspect: _viewport.aspectRatio,
      abi: abi,
      quality: _quality,
    );
    final int status = api.init(
      _newSeed(),
      _renderSize.width,
      _renderSize.height,
    );
    if (status != odgStatusOk) {
      throw OdgNativeException('Engine initialization failed', status);
    }
    api.setVisualTheme(odgVisualThemeNeonTides);
    api.setPresentationMode(odgPresentationShowcase);
    _syncTicker();
  }

  void play() {
    input.clear();
    api.reset(_newSeed());
    api.setPresentationMode(odgPresentationGameplay);
    _started = true;
    _previousElapsed = null;
    notifyListeners();
  }

  void restart() => play();

  void showMenu() {
    input.clear();
    api.setPresentationMode(odgPresentationShowcase);
    _started = false;
    _previousElapsed = null;
    notifyListeners();
  }

  void setTheme(int theme) {
    api.setVisualTheme(theme.clamp(0, odgVisualThemeCount - 1).toInt());
    notifyListeners();
  }

  void setQuality(RasterQuality quality) {
    if (_quality == quality) return;
    _quality = quality;
    _governor.reset();
    _applyRenderSize();
    notifyListeners();
  }

  void setViewport(Size viewport) {
    if (viewport.width <= 0 || viewport.height <= 0) return;
    final bool materiallyChanged =
        (_viewport.aspectRatio - viewport.aspectRatio).abs() > 0.002;
    _viewport = viewport;
    if (materiallyChanged) _applyRenderSize();
  }

  void setLifecycleActive(bool active) {
    if (_lifecycleActive == active) return;
    _lifecycleActive = active;
    if (!active) input.clear();
    _syncTicker();
  }

  void setHostPaused(bool paused) {
    if (_hostPaused == paused) return;
    _hostPaused = paused;
    if (paused) input.clear();
    _syncTicker();
  }

  void _syncTicker() {
    final bool shouldRun = _lifecycleActive && !_hostPaused && !_disposed;
    _previousElapsed = null;
    if (shouldRun && !_ticker.isActive) {
      _ticker.start();
    } else if (!shouldRun && _ticker.isActive) {
      _ticker.stop();
      api.setInput(0, 0, 0, 0, 0);
    }
  }

  void _onTick(Duration elapsed) {
    if (_disposed || !_lifecycleActive || _hostPaused) return;
    final Duration? previous = _previousElapsed;
    _previousElapsed = elapsed;
    if (previous == null) return;
    final int elapsedUs = (elapsed - previous).inMicroseconds
        .clamp(0, 50000)
        .toInt();
    final GameInputSample sample = input.sample();
    api.setInput(
      sample.moveXQ15,
      sample.moveForwardQ15,
      sample.lookXQ15,
      sample.lookYQ15,
      sample.buttons,
    );
    api.tickUs(elapsedUs);
    if (!_decodePending) _renderAndPublish();
  }

  void _renderAndPublish() {
    final Stopwatch watch = Stopwatch()..start();
    if (api.renderFrame() == 0) return;
    final int width = api.renderWidth();
    final int height = api.renderHeight();
    final int bytes = api.framebufferBytes();
    final int stride = api.framebufferStrideBytes();
    if (width <= 0 ||
        height <= 0 ||
        stride != width * 4 ||
        bytes != stride * height ||
        bytes > _frameCapacity) {
      throw const OdgNativeException('Native framebuffer metadata is invalid.');
    }
    _required.value = 0;
    final int copyStatus = api.copyFramebuffer(
      _frameBuffer,
      _frameCapacity,
      _required,
    );
    if (copyStatus != odgStatusOk || _required.value != bytes) {
      throw OdgNativeException('Framebuffer copy failed', copyStatus);
    }
    final Uint8List pixels = Uint8List.fromList(
      _frameBuffer.asTypedList(bytes),
    );
    _snapshot = _readSnapshot();
    watch.stop();
    if (_governor.observe(watch.elapsedMicroseconds)) _applyRenderSize();

    _decodePending = true;
    unawaited(_decodeAndPublish(pixels, width, height, stride));
  }

  Future<void> _decodeAndPublish(
    Uint8List pixels,
    int width,
    int height,
    int stride,
  ) async {
    ui.ImmutableBuffer? buffer;
    ui.ImageDescriptor? descriptor;
    ui.Codec? codec;
    ui.Image? decoded;
    try {
      buffer = await ui.ImmutableBuffer.fromUint8List(pixels);
      descriptor = ui.ImageDescriptor.raw(
        buffer,
        width: width,
        height: height,
        rowBytes: stride,
        pixelFormat: ui.PixelFormat.rgba8888,
      );
      codec = await descriptor.instantiateCodec();
      final ui.FrameInfo frame = await codec.getNextFrame();
      decoded = frame.image;
    } on Object catch (error, stackTrace) {
      if (!_disposed) {
        FlutterError.reportError(
          FlutterErrorDetails(
            exception: error,
            stack: stackTrace,
            library: 'ODPAR native framebuffer',
            context: ErrorDescription('while decoding an RGBA8 frame'),
          ),
        );
      }
    } finally {
      codec?.dispose();
      descriptor?.dispose();
      buffer?.dispose();
    }

    if (_disposed) {
      decoded?.dispose();
      return;
    }
    _decodePending = false;
    if (decoded == null) return;
    final ui.Image? previous = _image;
    _image = decoded;
    previous?.dispose();
    notifyListeners();
  }

  GameSnapshot _readSnapshot() {
    _required.value = 0;
    final int status = api.copyStats(
      _statsBuffer,
      sizeOf<OdgGameStats>(),
      _required,
    );
    if (status != odgStatusOk ||
        _required.value != sizeOf<OdgGameStats>() ||
        _statsBuffer.ref.structSize != sizeOf<OdgGameStats>() ||
        _statsBuffer.ref.apiVersion != odgApiVersion) {
      throw OdgNativeException('Stats snapshot copy failed', status);
    }
    final int count = math.min(3, api.leaderCount()).toInt();
    final List<GameLeader> leaders = <GameLeader>[
      for (int rank = 0; rank < count; rank += 1)
        GameLeader(
          score: api.leaderScore(rank),
          nameCode: api.leaderNameCode(rank),
          isPlayer: api.leaderIsPlayer(rank) != 0,
        ),
    ];
    return GameSnapshot.fromNative(_statsBuffer.ref, leaders);
  }

  void _applyRenderSize() {
    final RenderSize requested = RasterBudget.resolve(
      viewportAspect: _viewport.aspectRatio,
      abi: abi,
      quality: _quality,
      adaptiveScale: _governor.scale,
    );
    if (requested == _renderSize) return;
    final int status = api.resize(requested.width, requested.height);
    if (status != odgStatusOk) {
      throw OdgNativeException('Native resize rejected $requested', status);
    }
    _renderSize = requested;
  }

  int _newSeed() {
    final math.Random random = math.Random.secure();
    final int high = random.nextInt(0x7fffffff);
    final int low = random.nextInt(0x7fffffff);
    final int seed = (high << 31) | low;
    return seed == 0 ? 1 : seed;
  }

  @override
  void dispose() {
    if (_disposed) return;
    _disposed = true;
    if (_ticker.isActive) _ticker.stop();
    _ticker.dispose();
    _image?.dispose();
    calloc.free(_required);
    calloc.free(_statsBuffer);
    calloc.free(_frameBuffer);
    super.dispose();
  }
}
