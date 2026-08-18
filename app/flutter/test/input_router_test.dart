import 'package:flutter/widgets.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:odpar_territorial_domain/src/input/input_router.dart';
import 'package:odpar_territorial_domain/src/native/odg_bindings.dart';

void main() {
  test('multitouch owns move, free-look and contextual actions separately', () {
    final MultiTouchInputRouter input = MultiTouchInputRouter();
    addTearDown(input.dispose);
    final ControlLayout layout = ControlLayout.forViewport(
      const Size(400, 800),
      EdgeInsets.zero,
    );
    input.updateLayout(layout);
    input.setActionsEnabled(action: true, drop: true);

    input.pointerDown(
      1,
      layout.joystickCenter + Offset(layout.joystickRadius * 0.8, 0),
    );
    input.pointerDown(2, const Offset(210, 330));
    input.pointerMove(2, const Offset(230, 310));
    input.pointerDown(3, layout.actionRect.center);
    input.pointerDown(4, layout.dropRect.center);

    final GameInputSample first = input.sample();
    expect(first.moveX, greaterThan(0.7));
    expect(first.moveForward.abs(), lessThan(0.01));
    expect(first.lookX, greaterThan(0));
    expect(first.lookY, lessThan(0));
    expect(first.buttons & odgButtonAction, isNot(0));
    expect(first.buttons & odgButtonDrop, isNot(0));

    final GameInputSample second = input.sample();
    expect(second.buttons & odgButtonAction, isNot(0));
    expect(second.buttons & odgButtonDrop, isNot(0));
    expect(input.sample().buttons, 0);
  });

  test('clear neutralizes every held pointer and pulse', () {
    final MultiTouchInputRouter input = MultiTouchInputRouter();
    addTearDown(input.dispose);
    final ControlLayout layout = ControlLayout.forViewport(
      const Size(800, 400),
      const EdgeInsets.only(left: 12, right: 12, bottom: 8),
    );
    input.updateLayout(layout);
    input.setActionsEnabled(action: true, drop: true);
    input.pointerDown(
      7,
      layout.joystickCenter - Offset(0, layout.joystickRadius),
    );
    input.pointerDown(8, layout.actionRect.center);

    input.clear();
    final GameInputSample sample = input.sample();
    expect(sample.moveX, 0);
    expect(sample.moveForward, 0);
    expect(sample.lookX, 0);
    expect(sample.lookY, 0);
    expect(sample.buttons, 0);
  });

  test('disabled action regions remain available for free-look', () {
    final MultiTouchInputRouter input = MultiTouchInputRouter();
    addTearDown(input.dispose);
    final ControlLayout layout = ControlLayout.forViewport(
      const Size(400, 800),
      EdgeInsets.zero,
    );
    input.updateLayout(layout);
    input.setActionsEnabled(action: false, drop: false);
    input.pointerDown(9, layout.actionRect.center);
    input.pointerMove(9, layout.actionRect.center + const Offset(18, 4));

    final GameInputSample sample = input.sample();
    expect(sample.buttons, 0);
    expect(sample.lookX, greaterThan(0));
  });
}
