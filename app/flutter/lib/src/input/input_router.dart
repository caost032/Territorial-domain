import 'dart:math' as math;

import 'package:flutter/foundation.dart';
import 'package:flutter/widgets.dart';

import '../native/odg_bindings.dart';

final class ControlLayout {
  const ControlLayout({
    required this.viewport,
    required this.safePadding,
    required this.joystickCenter,
    required this.joystickRadius,
    required this.actionRect,
    required this.dropRect,
  });

  factory ControlLayout.forViewport(
    Size viewport,
    EdgeInsets safePadding, {
    bool moveOnLeft = true,
  }) {
    final double shortest = math.min(viewport.width, viewport.height);
    final double radius = (shortest * 0.105).clamp(48, 70).toDouble();
    final double sideInset = radius + 20;
    final double bottom = safePadding.bottom + radius + 22;
    final double joystickX = moveOnLeft
        ? safePadding.left + sideInset
        : viewport.width - safePadding.right - sideInset;
    final double actionDiameter = (radius * 0.96).clamp(54, 68).toDouble();
    final double actionX = moveOnLeft
        ? viewport.width - safePadding.right - 18 - actionDiameter
        : safePadding.left + 18;
    final double actionY = viewport.height - safePadding.bottom - 18 -
        actionDiameter;
    final Rect action = Rect.fromLTWH(
      actionX,
      actionY,
      actionDiameter,
      actionDiameter,
    );
    final Rect drop = Rect.fromLTWH(
      actionX - 2,
      action.top - 52,
      actionDiameter + 4,
      36,
    );
    return ControlLayout(
      viewport: viewport,
      safePadding: safePadding,
      joystickCenter: Offset(joystickX, viewport.height - bottom),
      joystickRadius: radius,
      actionRect: action,
      dropRect: drop,
    );
  }

  final Size viewport;
  final EdgeInsets safePadding;
  final Offset joystickCenter;
  final double joystickRadius;
  final Rect actionRect;
  final Rect dropRect;
}

enum _PointerRole { move, look, action, drop }

final class GameInputSample {
  const GameInputSample({
    required this.moveX,
    required this.moveForward,
    required this.lookX,
    required this.lookY,
    required this.buttons,
  });

  final double moveX;
  final double moveForward;
  final double lookX;
  final double lookY;
  final int buttons;

  int get moveXQ15 =>
      (moveX * 32767).round().clamp(-32767, 32767).toInt();
  int get moveForwardQ15 =>
      (moveForward * 32767).round().clamp(-32767, 32767).toInt();
  int get lookXQ15 =>
      (lookX * 32767).round().clamp(-32767, 32767).toInt();
  int get lookYQ15 =>
      (lookY * 32767).round().clamp(-32767, 32767).toInt();
}

final class MultiTouchInputRouter extends ChangeNotifier {
  ControlLayout? _layout;
  final Map<int, _PointerRole> _roles = <int, _PointerRole>{};
  final Map<int, Offset> _lastPositions = <int, Offset>{};
  int? _movePointer;
  int? _lookPointer;
  Offset _moveVector = Offset.zero;
  double _lookX = 0;
  double _lookY = 0;
  int _actionPulseFrames = 0;
  int _dropPulseFrames = 0;
  bool _actionEnabled = false;
  bool _dropEnabled = false;

  Offset get moveVector => _moveVector;
  bool get actionPressed => _actionPulseFrames > 0;
  bool get dropPressed => _dropPulseFrames > 0;

  void updateLayout(ControlLayout layout) {
    _layout = layout;
    if (_movePointer != null) _updateMove(_lastPositions[_movePointer!]);
  }

  void setActionsEnabled({required bool action, required bool drop}) {
    _actionEnabled = action;
    _dropEnabled = drop;
  }

  void pointerDown(int pointer, Offset position) {
    final ControlLayout? layout = _layout;
    if (layout == null || _roles.containsKey(pointer)) return;
    if (_actionEnabled && layout.actionRect.contains(position)) {
      _roles[pointer] = _PointerRole.action;
      _actionPulseFrames = 2;
    } else if (_dropEnabled && layout.dropRect.contains(position)) {
      _roles[pointer] = _PointerRole.drop;
      _dropPulseFrames = 2;
    } else if (_movePointer == null &&
        (position - layout.joystickCenter).distance <=
            layout.joystickRadius * 1.42) {
      _roles[pointer] = _PointerRole.move;
      _movePointer = pointer;
      _lastPositions[pointer] = position;
      _updateMove(position);
    } else if (_lookPointer == null) {
      _roles[pointer] = _PointerRole.look;
      _lookPointer = pointer;
      _lastPositions[pointer] = position;
    }
    notifyListeners();
  }

  void pointerMove(int pointer, Offset position) {
    final _PointerRole? role = _roles[pointer];
    if (role == _PointerRole.move) {
      _lastPositions[pointer] = position;
      _updateMove(position);
      notifyListeners();
    } else if (role == _PointerRole.look) {
      final Offset previous = _lastPositions[pointer] ?? position;
      final Offset delta = position - previous;
      _lastPositions[pointer] = position;
      _lookX = (_lookX + delta.dx * 0.026).clamp(-1, 1).toDouble();
      _lookY = (_lookY + delta.dy * 0.026).clamp(-1, 1).toDouble();
    }
  }

  void pointerUp(int pointer) {
    final _PointerRole? role = _roles.remove(pointer);
    _lastPositions.remove(pointer);
    if (role == _PointerRole.move) {
      _movePointer = null;
      _moveVector = Offset.zero;
    } else if (role == _PointerRole.look) {
      _lookPointer = null;
    }
    notifyListeners();
  }

  GameInputSample sample() {
    int buttons = 0;
    if (_actionPulseFrames > 0) {
      buttons |= odgButtonAction;
      _actionPulseFrames -= 1;
    }
    if (_dropPulseFrames > 0) {
      buttons |= odgButtonDrop;
      _dropPulseFrames -= 1;
    }
    final GameInputSample result = GameInputSample(
      moveX: _moveVector.dx,
      moveForward: -_moveVector.dy,
      lookX: _lookX,
      lookY: _lookY,
      buttons: buttons,
    );
    _lookX *= 0.44;
    _lookY *= 0.44;
    if (_lookX.abs() < 0.003) _lookX = 0;
    if (_lookY.abs() < 0.003) _lookY = 0;
    return result;
  }

  void clear() {
    _roles.clear();
    _lastPositions.clear();
    _movePointer = null;
    _lookPointer = null;
    _moveVector = Offset.zero;
    _lookX = 0;
    _lookY = 0;
    _actionPulseFrames = 0;
    _dropPulseFrames = 0;
    notifyListeners();
  }

  void _updateMove(Offset? position) {
    final ControlLayout? layout = _layout;
    if (layout == null || position == null) {
      _moveVector = Offset.zero;
      return;
    }
    final Offset raw = (position - layout.joystickCenter) /
        layout.joystickRadius;
    final double magnitude = raw.distance;
    if (magnitude <= 0.075) {
      _moveVector = Offset.zero;
      return;
    }
    final double normalizedMagnitude =
        ((math.min(1, magnitude) - 0.075) / 0.925)
            .clamp(0, 1)
            .toDouble();
    _moveVector = raw / magnitude * normalizedMagnitude;
  }
}
