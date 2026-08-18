import 'package:flutter_test/flutter_test.dart';
import 'package:odpar_territorial_domain/src/native/odg_bindings.dart';
import 'package:odpar_territorial_domain/src/render/raster_budget.dart';

void main() {
  const OdgAbiDescriptor abi = OdgAbiDescriptor(
    maxRenderWidth: 1280,
    maxRenderHeight: 1280,
    maxRenderPixels: 1280 * 720,
    featureBits: 0,
  );

  test('raster budget preserves portrait and landscape aspects', () {
    final RenderSize portrait = RasterBudget.resolve(
      viewportAspect: 9 / 16,
      abi: abi,
      quality: RasterQuality.ultra,
    );
    final RenderSize landscape = RasterBudget.resolve(
      viewportAspect: 16 / 9,
      abi: abi,
      quality: RasterQuality.ultra,
    );

    expect(portrait.aspectRatio, closeTo(9 / 16, 0.01));
    expect(landscape.aspectRatio, closeTo(16 / 9, 0.01));
    expect(portrait.pixels, lessThanOrEqualTo(abi.maxRenderPixels));
    expect(landscape.pixels, lessThanOrEqualTo(abi.maxRenderPixels));
    expect(portrait.width, lessThanOrEqualTo(abi.maxRenderWidth));
    expect(portrait.height, lessThanOrEqualTo(abi.maxRenderHeight));
  });

  test('quality tiers never change aspect or ABI limits', () {
    for (final RasterQuality quality in RasterQuality.values) {
      final RenderSize size = RasterBudget.resolve(
        viewportAspect: 1080 / 2340,
        abi: abi,
        quality: quality,
        adaptiveScale: 0.58,
      );
      expect(size.aspectRatio, closeTo(1080 / 2340, 0.012));
      expect(size.pixels, lessThanOrEqualTo(abi.maxRenderPixels));
      expect(size.width.isEven, isTrue);
      expect(size.height.isEven, isTrue);
    }
  });

  test('governor steps down quickly and recovers with hysteresis', () {
    final RasterGovernor governor = RasterGovernor(windowSize: 2);
    expect(governor.scale, 1);
    expect(governor.observe(20000), isFalse);
    expect(governor.observe(20000), isTrue);
    expect(governor.scale, 0.86);

    for (int window = 0; window < 2; window += 1) {
      expect(governor.observe(4000), isFalse);
      expect(governor.observe(4000), isFalse);
    }
    expect(governor.observe(4000), isFalse);
    expect(governor.observe(4000), isTrue);
    expect(governor.scale, 1);
  });
}
