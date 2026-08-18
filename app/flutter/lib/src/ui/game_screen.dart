import 'dart:ui' as ui;

import 'package:flutter/material.dart';
import 'package:flutter/scheduler.dart';

import '../engine/game_runtime.dart';
import '../engine/game_snapshot.dart';
import '../input/input_router.dart';
import '../native/odg_bindings.dart';
import '../render/raster_budget.dart';
import 'design_system.dart';

final class GameScreen extends StatefulWidget {
  const GameScreen({required this.runtime, required this.input, super.key});

  final GameRuntime runtime;
  final MultiTouchInputRouter input;

  @override
  State<GameScreen> createState() => _GameScreenState();
}

final class _GameScreenState extends State<GameScreen>
    with WidgetsBindingObserver {
  late final Listenable _animation;
  bool _menuOpen = true;
  bool _settingsOpen = false;
  bool _moveOnLeft = true;
  int _theme = odgVisualThemeNeonTides;
  Size? _queuedSize;
  EdgeInsets? _queuedPadding;
  bool _layoutCallbackScheduled = false;

  @override
  void initState() {
    super.initState();
    WidgetsBinding.instance.addObserver(this);
    _animation = Listenable.merge(<Listenable>[widget.runtime, widget.input]);
    widget.runtime.setTheme(_theme);
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    widget.runtime.setLifecycleActive(state == AppLifecycleState.resumed);
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    super.dispose();
  }

  void _queueLayout(Size size, EdgeInsets padding) {
    if (_queuedSize == size && _queuedPadding == padding) return;
    _queuedSize = size;
    _queuedPadding = padding;
    if (_layoutCallbackScheduled) return;
    _layoutCallbackScheduled = true;
    SchedulerBinding.instance.addPostFrameCallback((Duration _) {
      _layoutCallbackScheduled = false;
      if (!mounted) return;
      final Size? queuedSize = _queuedSize;
      final EdgeInsets? queuedPadding = _queuedPadding;
      if (queuedSize == null || queuedPadding == null) return;
      widget.input.updateLayout(
        ControlLayout.forViewport(
          queuedSize,
          queuedPadding,
          moveOnLeft: _moveOnLeft,
        ),
      );
      widget.runtime.setViewport(queuedSize);
    });
  }

  void _startMatch() {
    widget.runtime.setHostPaused(false);
    widget.runtime.play();
    setState(() {
      _settingsOpen = false;
      _menuOpen = false;
    });
  }

  void _openSettings() {
    widget.runtime.setHostPaused(true);
    setState(() => _settingsOpen = true);
  }

  void _closeSettings() {
    widget.runtime.setHostPaused(false);
    setState(() => _settingsOpen = false);
  }

  void _returnToMenu() {
    widget.runtime.setHostPaused(false);
    widget.runtime.showMenu();
    setState(() {
      _settingsOpen = false;
      _menuOpen = true;
    });
  }

  void _setMoveOnLeft(bool value) {
    if (_moveOnLeft == value) return;
    setState(() => _moveOnLeft = value);
    final Size? size = _queuedSize;
    final EdgeInsets? padding = _queuedPadding;
    if (size != null && padding != null) {
      widget.input.clear();
      widget.input.updateLayout(
        ControlLayout.forViewport(size, padding, moveOnLeft: value),
      );
    }
  }

  void _setTheme(int value) {
    setState(() => _theme = value);
    widget.runtime.setTheme(value);
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: LayoutBuilder(
        builder: (BuildContext context, BoxConstraints constraints) {
          final Size viewport = constraints.biggest;
          final EdgeInsets padding = MediaQuery.paddingOf(context);
          _queueLayout(viewport, padding);
          return AnimatedBuilder(
            animation: _animation,
            builder: (BuildContext context, Widget? child) {
              return _buildFrame(context, viewport, padding);
            },
          );
        },
      ),
    );
  }

  Widget _buildFrame(BuildContext context, Size viewport, EdgeInsets padding) {
    final GameSnapshot snapshot = widget.runtime.snapshot;
    final ControlLayout controls = ControlLayout.forViewport(
      viewport,
      padding,
      moveOnLeft: _moveOnLeft,
    );
    final bool interactive =
        widget.runtime.started &&
        !_menuOpen &&
        !_settingsOpen &&
        snapshot.playerAlive &&
        !snapshot.matchOver;
    final bool actionEnabled =
        interactive &&
        (snapshot.turretActionAvailable || snapshot.hackActionAvailable);
    final bool dropEnabled = interactive && snapshot.dropActionAvailable;
    widget.input.setActionsEnabled(action: actionEnabled, drop: dropEnabled);

    return Listener(
      behavior: HitTestBehavior.opaque,
      onPointerDown: interactive
          ? (PointerDownEvent event) {
              widget.input.pointerDown(event.pointer, event.localPosition);
            }
          : null,
      onPointerMove: interactive
          ? (PointerMoveEvent event) {
              widget.input.pointerMove(event.pointer, event.localPosition);
            }
          : null,
      onPointerUp: interactive
          ? (PointerUpEvent event) => widget.input.pointerUp(event.pointer)
          : null,
      onPointerCancel: interactive
          ? (PointerCancelEvent event) => widget.input.pointerUp(event.pointer)
          : null,
      child: Stack(
        fit: StackFit.expand,
        children: <Widget>[
          _NativeFrame(image: widget.runtime.image),
          const _AtmosphereOverlay(),
          if (!_menuOpen) _buildHud(snapshot, padding, viewport),
          if (interactive)
            IgnorePointer(
              child: CustomPaint(
                painter: _ControlPainter(
                  layout: controls,
                  moveVector: widget.input.moveVector,
                  actionEnabled: actionEnabled,
                  actionPressed: widget.input.actionPressed,
                  actionLabel: snapshot.contextualActionLabel,
                  dropEnabled: dropEnabled,
                  dropPressed: widget.input.dropPressed,
                ),
              ),
            ),
          if (_menuOpen)
            _MainMenu(
              quality: widget.runtime.quality,
              theme: _theme,
              onQualityChanged: widget.runtime.setQuality,
              onThemeChanged: _setTheme,
              onStart: _startMatch,
            ),
          if (_settingsOpen)
            _SettingsPanel(
              quality: widget.runtime.quality,
              theme: _theme,
              moveOnLeft: _moveOnLeft,
              onQualityChanged: widget.runtime.setQuality,
              onThemeChanged: _setTheme,
              onMoveSideChanged: _setMoveOnLeft,
              onResume: _closeSettings,
              onRestart: _startMatch,
              onExit: _returnToMenu,
            ),
          if (!_menuOpen && !_settingsOpen && snapshot.matchOver)
            _ResultPanel(snapshot: snapshot, onRestart: _startMatch),
        ],
      ),
    );
  }

  Widget _buildHud(GameSnapshot snapshot, EdgeInsets padding, Size viewport) {
    final bool landscape = viewport.width > viewport.height;
    final double side = padding.left + 14;
    final double top = padding.top + 12;
    return Stack(
      children: <Widget>[
        Positioned(
          left: side,
          top: top,
          child: _TerritoryReadout(snapshot: snapshot),
        ),
        Positioned(
          right: padding.right + 14,
          top: top,
          child: Row(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: <Widget>[
              if (landscape) ...<Widget>[
                _Leaderboard(snapshot: snapshot),
                const SizedBox(width: 10),
              ],
              _SquareIconButton(
                icon: Icons.tune_rounded,
                tooltip: 'Ajustes',
                onPressed: _openSettings,
              ),
            ],
          ),
        ),
        if (!landscape)
          Positioned(
            right: padding.right + 14,
            top: top + 52,
            child: _Leaderboard(snapshot: snapshot),
          ),
        Positioned(
          left: padding.left + 18,
          right: padding.right + 18,
          bottom: padding.bottom + 16,
          child: Center(child: _StatusLine(snapshot: snapshot)),
        ),
      ],
    );
  }
}

final class _NativeFrame extends StatelessWidget {
  const _NativeFrame({required this.image});

  final ui.Image? image;

  @override
  Widget build(BuildContext context) {
    final ui.Image? frame = image;
    if (frame == null) {
      return const DecoratedBox(
        decoration: BoxDecoration(
          gradient: RadialGradient(
            radius: 1.1,
            colors: <Color>[Color(0xFF172028), OdparDesign.voidBlack],
          ),
        ),
      );
    }
    return RawImage(
      image: frame,
      fit: BoxFit.cover,
      filterQuality: FilterQuality.low,
    );
  }
}

final class _AtmosphereOverlay extends StatelessWidget {
  const _AtmosphereOverlay();

  @override
  Widget build(BuildContext context) {
    return const IgnorePointer(
      child: DecoratedBox(
        decoration: BoxDecoration(
          gradient: LinearGradient(
            begin: Alignment.topCenter,
            end: Alignment.bottomCenter,
            colors: <Color>[
              Color(0xA6000000),
              Color(0x05000000),
              Color(0x18000000),
              Color(0x9C000000),
            ],
            stops: <double>[0, 0.22, 0.64, 1],
          ),
        ),
      ),
    );
  }
}

final class _TerritoryReadout extends StatelessWidget {
  const _TerritoryReadout({required this.snapshot});

  final GameSnapshot snapshot;

  @override
  Widget build(BuildContext context) {
    return OdparPanel(
      padding: const EdgeInsets.fromLTRB(13, 10, 13, 9),
      child: SizedBox(
        width: 174,
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: <Widget>[
            Row(
              children: <Widget>[
                OdparMetric(
                  label: 'DOMINIO',
                  value: '${snapshot.territoryPercent.toStringAsFixed(1)}%',
                  emphasized: true,
                ),
                const Spacer(),
                OdparMetric(label: 'EN RED', value: '${snapshot.aliveCount}'),
              ],
            ),
            const SizedBox(height: 8),
            ClipRRect(
              borderRadius: BorderRadius.circular(2),
              child: LinearProgressIndicator(
                value: (snapshot.territoryPermille / 1000)
                    .clamp(0, 1)
                    .toDouble(),
                minHeight: 3,
                color: OdparDesign.accent,
                backgroundColor: const Color(0xFF28313B),
              ),
            ),
          ],
        ),
      ),
    );
  }
}

final class _Leaderboard extends StatelessWidget {
  const _Leaderboard({required this.snapshot});

  final GameSnapshot snapshot;

  @override
  Widget build(BuildContext context) {
    return OdparPanel(
      padding: const EdgeInsets.symmetric(horizontal: 11, vertical: 8),
      child: SizedBox(
        width: 132,
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: <Widget>[
            for (int index = 0; index < snapshot.leaders.length; index += 1)
              _LeaderRow(
                rank: index + 1,
                leader: snapshot.leaders[index],
                percent: snapshot.leaderPercent(snapshot.leaders[index].score),
              ),
          ],
        ),
      ),
    );
  }
}

final class _LeaderRow extends StatelessWidget {
  const _LeaderRow({
    required this.rank,
    required this.leader,
    required this.percent,
  });

  final int rank;
  final GameLeader leader;
  final double percent;

  @override
  Widget build(BuildContext context) {
    final String name = leader.isPlayer
        ? 'TÚ'
        : GameRuntime.leaderNames[leader.nameCode %
              GameRuntime.leaderNames.length];
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 2),
      child: Row(
        children: <Widget>[
          SizedBox(
            width: 15,
            child: Text(
              '$rank',
              style: const TextStyle(
                color: OdparDesign.textMuted,
                fontSize: 10,
              ),
            ),
          ),
          Expanded(
            child: Text(
              name,
              overflow: TextOverflow.ellipsis,
              style: TextStyle(
                color: leader.isPlayer ? OdparDesign.accent : OdparDesign.text,
                fontSize: 10,
                fontWeight: FontWeight.w600,
                letterSpacing: 0.8,
              ),
            ),
          ),
          Text(
            '${percent.toStringAsFixed(1)}%',
            style: const TextStyle(
              color: OdparDesign.textMuted,
              fontSize: 10,
              fontFeatures: <ui.FontFeature>[ui.FontFeature.tabularFigures()],
            ),
          ),
        ],
      ),
    );
  }
}

final class _StatusLine extends StatelessWidget {
  const _StatusLine({required this.snapshot});

  final GameSnapshot snapshot;

  @override
  Widget build(BuildContext context) {
    String text = snapshot.statusLabel;
    Color color = OdparDesign.textMuted;
    if (!snapshot.playerAlive) {
      text = 'FUERA DE RED · SIMULACIÓN EN CURSO';
      color = OdparDesign.danger;
    } else if (snapshot.trailActive) {
      color = OdparDesign.amber;
    } else if (snapshot.nearbyTurretVisible) {
      text =
          'TORRETA  ${snapshot.nearbyTurretAmmo}'
          '/${snapshot.nearbyTurretMaxAmmo}  ·  ${snapshot.statusLabel}';
      color = OdparDesign.accent;
    }
    return DecoratedBox(
      decoration: BoxDecoration(
        color: const Color(0xC90C1015),
        borderRadius: BorderRadius.circular(4),
        border: Border.all(color: const Color(0xBB28313B)),
      ),
      child: Padding(
        padding: const EdgeInsets.symmetric(horizontal: 11, vertical: 6),
        child: Text(
          text,
          textAlign: TextAlign.center,
          style: TextStyle(
            color: color,
            fontSize: 9,
            fontWeight: FontWeight.w700,
            letterSpacing: 1.2,
          ),
        ),
      ),
    );
  }
}

final class _SquareIconButton extends StatelessWidget {
  const _SquareIconButton({
    required this.icon,
    required this.tooltip,
    required this.onPressed,
  });

  final IconData icon;
  final String tooltip;
  final VoidCallback onPressed;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: 42,
      height: 42,
      child: IconButton(
        tooltip: tooltip,
        style: IconButton.styleFrom(
          backgroundColor: OdparDesign.panel,
          side: const BorderSide(color: OdparDesign.panelEdge),
          shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(6)),
        ),
        onPressed: onPressed,
        icon: Icon(icon, size: 18),
      ),
    );
  }
}

final class _MainMenu extends StatelessWidget {
  const _MainMenu({
    required this.quality,
    required this.theme,
    required this.onQualityChanged,
    required this.onThemeChanged,
    required this.onStart,
  });

  final RasterQuality quality;
  final int theme;
  final ValueChanged<RasterQuality> onQualityChanged;
  final ValueChanged<int> onThemeChanged;
  final VoidCallback onStart;

  @override
  Widget build(BuildContext context) {
    return SafeArea(
      child: Center(
        child: SingleChildScrollView(
          padding: const EdgeInsets.all(18),
          child: ConstrainedBox(
            constraints: const BoxConstraints(maxWidth: 520),
            child: OdparPanel(
              padding: const EdgeInsets.fromLTRB(24, 22, 24, 24),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: <Widget>[
                  const Text(
                    'ODPAR / 14',
                    style: TextStyle(
                      color: OdparDesign.accent,
                      fontSize: 10,
                      fontWeight: FontWeight.w700,
                      letterSpacing: 2.6,
                    ),
                  ),
                  const SizedBox(height: 9),
                  Text(
                    'TERRITORIAL\nDOMAIN',
                    style: Theme.of(context).textTheme.displaySmall,
                  ),
                  const SizedBox(height: 15),
                  const Text(
                    'Conquista superficie, protege tu trazo y toma torretas '
                    'neutrales. El chip reprograma infraestructura enemiga.',
                  ),
                  const SizedBox(height: 20),
                  const _SectionLabel('RASTER'),
                  const SizedBox(height: 8),
                  _QualityStrip(value: quality, onChanged: onQualityChanged),
                  const SizedBox(height: 15),
                  const _SectionLabel('ATMÓSFERA'),
                  const SizedBox(height: 8),
                  _ThemeStrip(value: theme, onChanged: onThemeChanged),
                  const SizedBox(height: 22),
                  OdparActionButton(
                    label: 'ENTRAR AL DOMINIO',
                    onPressed: onStart,
                    primary: true,
                  ),
                  const SizedBox(height: 12),
                  const Text(
                    'JOYSTICK PARA MOVER · ARRASTRA PARA MIRAR · '
                    'ACCIÓN Y SOLTAR SON CONTEXTUALES',
                    textAlign: TextAlign.center,
                    style: TextStyle(
                      color: OdparDesign.textMuted,
                      fontSize: 8,
                      fontWeight: FontWeight.w600,
                      letterSpacing: 1.1,
                    ),
                  ),
                ],
              ),
            ),
          ),
        ),
      ),
    );
  }
}

final class _SettingsPanel extends StatelessWidget {
  const _SettingsPanel({
    required this.quality,
    required this.theme,
    required this.moveOnLeft,
    required this.onQualityChanged,
    required this.onThemeChanged,
    required this.onMoveSideChanged,
    required this.onResume,
    required this.onRestart,
    required this.onExit,
  });

  final RasterQuality quality;
  final int theme;
  final bool moveOnLeft;
  final ValueChanged<RasterQuality> onQualityChanged;
  final ValueChanged<int> onThemeChanged;
  final ValueChanged<bool> onMoveSideChanged;
  final VoidCallback onResume;
  final VoidCallback onRestart;
  final VoidCallback onExit;

  @override
  Widget build(BuildContext context) {
    return ColoredBox(
      color: const Color(0x99000000),
      child: SafeArea(
        child: Center(
          child: SingleChildScrollView(
            padding: const EdgeInsets.all(18),
            child: ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 500),
              child: OdparPanel(
                padding: const EdgeInsets.all(22),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: <Widget>[
                    Row(
                      children: <Widget>[
                        Text(
                          'SISTEMA',
                          style: Theme.of(context).textTheme.headlineSmall,
                        ),
                        const Spacer(),
                        _SquareIconButton(
                          icon: Icons.close_rounded,
                          tooltip: 'Continuar',
                          onPressed: onResume,
                        ),
                      ],
                    ),
                    const SizedBox(height: 20),
                    const _SectionLabel('CALIDAD DE RASTER'),
                    const SizedBox(height: 8),
                    _QualityStrip(value: quality, onChanged: onQualityChanged),
                    const SizedBox(height: 16),
                    const _SectionLabel('ATMÓSFERA'),
                    const SizedBox(height: 8),
                    _ThemeStrip(value: theme, onChanged: onThemeChanged),
                    const SizedBox(height: 16),
                    const _SectionLabel('LADO DE MOVIMIENTO'),
                    const SizedBox(height: 8),
                    _OptionStrip<bool>(
                      values: const <bool>[true, false],
                      value: moveOnLeft,
                      labelFor: (bool item) => item ? 'IZQUIERDA' : 'DERECHA',
                      onChanged: onMoveSideChanged,
                    ),
                    const SizedBox(height: 22),
                    Row(
                      children: <Widget>[
                        Expanded(
                          child: OdparActionButton(
                            label: 'REINICIAR',
                            onPressed: onRestart,
                          ),
                        ),
                        const SizedBox(width: 10),
                        Expanded(
                          child: OdparActionButton(
                            label: 'CONTINUAR',
                            onPressed: onResume,
                            primary: true,
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 10),
                    TextButton(
                      onPressed: onExit,
                      child: const Text('VOLVER AL CENTRO DE MANDO'),
                    ),
                  ],
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}

final class _ResultPanel extends StatelessWidget {
  const _ResultPanel({required this.snapshot, required this.onRestart});

  final GameSnapshot snapshot;
  final VoidCallback onRestart;

  @override
  Widget build(BuildContext context) {
    final bool playerWon = snapshot.winnerId == 0;
    return ColoredBox(
      color: const Color(0xA8000000),
      child: SafeArea(
        child: Center(
          child: Padding(
            padding: const EdgeInsets.all(20),
            child: ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 410),
              child: OdparPanel(
                padding: const EdgeInsets.all(24),
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: <Widget>[
                    Text(
                      playerWon ? 'DOMINIO ASEGURADO' : 'SECTOR PERDIDO',
                      textAlign: TextAlign.center,
                      style: Theme.of(context).textTheme.headlineSmall,
                    ),
                    const SizedBox(height: 12),
                    Text(
                      '${snapshot.territoryPercent.toStringAsFixed(1)}% '
                      'de superficie · ${snapshot.ownedTurrets} torretas',
                      textAlign: TextAlign.center,
                    ),
                    const SizedBox(height: 20),
                    OdparActionButton(
                      label: 'NUEVO DESPLIEGUE',
                      onPressed: onRestart,
                      primary: true,
                    ),
                  ],
                ),
              ),
            ),
          ),
        ),
      ),
    );
  }
}

final class _SectionLabel extends StatelessWidget {
  const _SectionLabel(this.text);

  final String text;

  @override
  Widget build(BuildContext context) {
    return Text(
      text,
      style: const TextStyle(
        color: OdparDesign.textMuted,
        fontSize: 9,
        fontWeight: FontWeight.w700,
        letterSpacing: 1.8,
      ),
    );
  }
}

final class _QualityStrip extends StatelessWidget {
  const _QualityStrip({required this.value, required this.onChanged});

  final RasterQuality value;
  final ValueChanged<RasterQuality> onChanged;

  @override
  Widget build(BuildContext context) {
    return _OptionStrip<RasterQuality>(
      values: RasterQuality.values,
      value: value,
      labelFor: (RasterQuality item) => item.label,
      onChanged: onChanged,
    );
  }
}

final class _ThemeStrip extends StatelessWidget {
  const _ThemeStrip({required this.value, required this.onChanged});

  static const List<String> _labels = <String>[
    'BASALTO',
    'CANOPIA',
    'COBRE',
    'MEDIANOCHE',
  ];

  final int value;
  final ValueChanged<int> onChanged;

  @override
  Widget build(BuildContext context) {
    return _OptionStrip<int>(
      values: const <int>[0, 1, 2, 3],
      value: value,
      labelFor: (int item) => _labels[item],
      onChanged: onChanged,
    );
  }
}

final class _OptionStrip<T> extends StatelessWidget {
  const _OptionStrip({
    required this.values,
    required this.value,
    required this.labelFor,
    required this.onChanged,
  });

  final List<T> values;
  final T value;
  final String Function(T) labelFor;
  final ValueChanged<T> onChanged;

  @override
  Widget build(BuildContext context) {
    return LayoutBuilder(
      builder: (BuildContext context, BoxConstraints constraints) {
        final int columns = constraints.maxWidth < 380 ? 2 : values.length;
        final double width =
            (constraints.maxWidth - (columns - 1) * 7) / columns;
        return Wrap(
          spacing: 7,
          runSpacing: 7,
          children: <Widget>[
            for (final T item in values)
              SizedBox(
                width: width,
                height: 36,
                child: InkWell(
                  borderRadius: BorderRadius.circular(4),
                  onTap: () => onChanged(item),
                  child: DecoratedBox(
                    decoration: BoxDecoration(
                      color: item == value
                          ? const Color(0xFF25332F)
                          : const Color(0xFF151A20),
                      borderRadius: BorderRadius.circular(4),
                      border: Border.all(
                        color: item == value
                            ? OdparDesign.accent
                            : OdparDesign.panelEdge,
                      ),
                    ),
                    child: Center(
                      child: Text(
                        labelFor(item),
                        maxLines: 1,
                        overflow: TextOverflow.fade,
                        style: TextStyle(
                          color: item == value
                              ? OdparDesign.accent
                              : OdparDesign.textMuted,
                          fontSize: 8,
                          fontWeight: FontWeight.w700,
                          letterSpacing: 0.7,
                        ),
                      ),
                    ),
                  ),
                ),
              ),
          ],
        );
      },
    );
  }
}

final class _ControlPainter extends CustomPainter {
  const _ControlPainter({
    required this.layout,
    required this.moveVector,
    required this.actionEnabled,
    required this.actionPressed,
    required this.actionLabel,
    required this.dropEnabled,
    required this.dropPressed,
  });

  final ControlLayout layout;
  final Offset moveVector;
  final bool actionEnabled;
  final bool actionPressed;
  final String actionLabel;
  final bool dropEnabled;
  final bool dropPressed;

  @override
  void paint(Canvas canvas, Size size) {
    final Paint line = Paint()
      ..style = PaintingStyle.stroke
      ..strokeWidth = 1.2
      ..color = OdparDesign.text.withValues(alpha: 0.24);
    final Paint fill = Paint()
      ..style = PaintingStyle.fill
      ..color = OdparDesign.voidBlack.withValues(alpha: 0.38);
    canvas.drawCircle(layout.joystickCenter, layout.joystickRadius, fill);
    canvas.drawCircle(layout.joystickCenter, layout.joystickRadius, line);
    final Offset knob =
        layout.joystickCenter + moveVector * (layout.joystickRadius * 0.62);
    canvas.drawCircle(
      knob,
      layout.joystickRadius * 0.29,
      Paint()
        ..color = OdparDesign.accent.withValues(alpha: 0.24)
        ..style = PaintingStyle.fill,
    );
    canvas.drawCircle(
      knob,
      layout.joystickRadius * 0.29,
      Paint()
        ..color = OdparDesign.accent.withValues(alpha: 0.72)
        ..style = PaintingStyle.stroke
        ..strokeWidth = 1.3,
    );

    if (actionEnabled) {
      final Offset center = layout.actionRect.center;
      final double radius = layout.actionRect.shortestSide / 2;
      canvas.drawCircle(
        center,
        radius,
        Paint()
          ..color = OdparDesign.accent.withValues(
            alpha: actionPressed ? 0.38 : 0.15,
          ),
      );
      canvas.drawCircle(
        center,
        radius,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = 1.3
          ..color = OdparDesign.accent.withValues(alpha: 0.82),
      );
      _paintLabel(canvas, center, actionLabel, OdparDesign.accent);
    }
    if (dropEnabled) {
      final RRect shape = RRect.fromRectAndRadius(
        layout.dropRect,
        const Radius.circular(4),
      );
      canvas.drawRRect(
        shape,
        Paint()
          ..color = OdparDesign.amber.withValues(
            alpha: dropPressed ? 0.34 : 0.13,
          ),
      );
      canvas.drawRRect(
        shape,
        Paint()
          ..style = PaintingStyle.stroke
          ..strokeWidth = 1
          ..color = OdparDesign.amber.withValues(alpha: 0.72),
      );
      _paintLabel(canvas, layout.dropRect.center, 'SOLTAR', OdparDesign.amber);
    }
  }

  void _paintLabel(Canvas canvas, Offset center, String text, Color color) {
    final TextPainter painter = TextPainter(
      text: TextSpan(
        text: text,
        style: TextStyle(
          color: color,
          fontSize: text.length > 8 ? 7 : 9,
          fontWeight: FontWeight.w700,
          letterSpacing: 0.8,
        ),
      ),
      maxLines: 1,
      textDirection: TextDirection.ltr,
    )..layout(maxWidth: layout.actionRect.width - 8);
    painter.paint(
      canvas,
      center - Offset(painter.width / 2, painter.height / 2),
    );
  }

  @override
  bool shouldRepaint(_ControlPainter oldDelegate) {
    return oldDelegate.layout.joystickCenter != layout.joystickCenter ||
        oldDelegate.layout.joystickRadius != layout.joystickRadius ||
        oldDelegate.moveVector != moveVector ||
        oldDelegate.actionEnabled != actionEnabled ||
        oldDelegate.actionPressed != actionPressed ||
        oldDelegate.actionLabel != actionLabel ||
        oldDelegate.dropEnabled != dropEnabled ||
        oldDelegate.dropPressed != dropPressed;
  }
}
