import 'dart:ffi';

import 'package:flutter_test/flutter_test.dart';
import 'package:odpar_territorial_domain/odpar_territorial_domain_ffi.dart';

void main() {
  test('Dart structs match frozen FFI ABI v1', () {
    expect(odgApiVersion, 14);
    expect(odgFfiAbiVersion, 1);
    expect(odgTickRate, 120);
    expect(sizeOf<OdgFfiAbiInfo>(), 64);
    expect(sizeOf<OdgGameStats>(), 192);
    expect(sizeOf<OdgLeaderEntry>(), 24);
  });

  test('required feature mask remains explicit', () {
    const int required =
        odgFfiFeatureFramebufferCopy |
        odgFfiFeatureStatsCopy |
        odgFfiFeaturePortraitRender |
        odgFfiFeatureFixed120Hz |
        odgFfiFeatureCameraInput;
    expect(required & odgFfiFeatureFramebufferCopy, isNot(0));
    expect(required & odgFfiFeatureStatsCopy, isNot(0));
    expect(required & odgFfiFeaturePortraitRender, isNot(0));
    expect(required & odgFfiFeatureFixed120Hz, isNot(0));
    expect(required & odgFfiFeatureCameraInput, isNot(0));
  });
}
