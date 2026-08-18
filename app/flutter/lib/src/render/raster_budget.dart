import 'dart:math' as math;

import '../native/odg_bindings.dart';

enum RasterQuality { efficient, balanced, high, ultra }

extension RasterQualityPresentation on RasterQuality {
  String get label => switch (this) {
        RasterQuality.efficient => 'EFICIENTE',
        RasterQuality.balanced => 'EQUILIBRADA',
        RasterQuality.high => 'ALTA',
        RasterQuality.ultra => 'ULTRA',
      };

  double get pixelFraction => switch (this) {
        RasterQuality.efficient => 0.16,
        RasterQuality.balanced => 0.30,
        RasterQuality.high => 0.58,
        RasterQuality.ultra => 1.0,
      };
}

final class RenderSize {
  const RenderSize(this.width, this.height);

  final int width;
  final int height;

  int get pixels => width * height;
  double get aspectRatio => width / height;

  @override
  bool operator ==(Object other) =>
      other is RenderSize && other.width == width && other.height == height;

  @override
  int get hashCode => Object.hash(width, height);

  @override
  String toString() => '${width}x$height';
}

final class RasterBudget {
  const RasterBudget._();

  static RenderSize resolve({
    required double viewportAspect,
    required OdgAbiDescriptor abi,
    required RasterQuality quality,
    double adaptiveScale = 1,
  }) {
    final double aspect = viewportAspect.clamp(0.25, 4).toDouble();
    final double scale = adaptiveScale.clamp(0.5, 1).toDouble();
    final int pixelBudget = math.max(
      16384,
      math.min(
        abi.maxRenderPixels,
        (abi.maxRenderPixels * quality.pixelFraction * scale * scale).floor(),
      ),
    ).toInt();

    double width = math.sqrt(pixelBudget * aspect);
    double height = width / aspect;
    final double dimensionScale = math.min(
      1.0,
      math.min(
        abi.maxRenderWidth / width,
        abi.maxRenderHeight / height,
      ),
    );
    width *= dimensionScale;
    height *= dimensionScale;

    int w = math.max(2, width.floor() & ~1).toInt();
    int h = math.max(2, height.floor() & ~1).toInt();
    while (w * h > abi.maxRenderPixels) {
      if (w / h > aspect) {
        w -= 2;
      } else {
        h -= 2;
      }
    }
    return RenderSize(w, h);
  }
}

final class RasterGovernor {
  RasterGovernor({
    this.windowSize = 24,
    this.slowThresholdUs = 14500,
    this.fastThresholdUs = 7600,
  });

  static const List<double> _steps = <double>[0.58, 0.72, 0.86, 1];

  final int windowSize;
  final int slowThresholdUs;
  final int fastThresholdUs;

  int _step = _steps.length - 1;
  int _samples = 0;
  int _sumUs = 0;
  int _fastWindows = 0;

  double get scale => _steps[_step];

  bool observe(int renderAndCopyUs) {
    _sumUs += renderAndCopyUs;
    _samples += 1;
    if (_samples < windowSize) return false;

    final int average = _sumUs ~/ _samples;
    _sumUs = 0;
    _samples = 0;
    if (average > slowThresholdUs && _step > 0) {
      _step -= 1;
      _fastWindows = 0;
      return true;
    }
    if (average < fastThresholdUs && _step < _steps.length - 1) {
      _fastWindows += 1;
      if (_fastWindows >= 3) {
        _step += 1;
        _fastWindows = 0;
        return true;
      }
    } else {
      _fastWindows = 0;
    }
    return false;
  }

  void reset() {
    _step = _steps.length - 1;
    _samples = 0;
    _sumUs = 0;
    _fastWindows = 0;
  }
}
