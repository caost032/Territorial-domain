import 'dart:ui' show FontFeature;

import 'package:flutter/material.dart';

abstract final class OdparDesign {
  static const Color voidBlack = Color(0xFF07090C);
  static const Color panel = Color(0xE610141A);
  static const Color panelEdge = Color(0xFF28313B);
  static const Color text = Color(0xFFF1F5F7);
  static const Color textMuted = Color(0xFFA1AAB2);
  static const Color accent = Color(0xFF74DEC9);
  static const Color amber = Color(0xFFE3B96B);
  static const Color danger = Color(0xFFEE7C78);

  static ThemeData get theme {
    final ColorScheme colors = ColorScheme.fromSeed(
      seedColor: accent,
      brightness: Brightness.dark,
    ).copyWith(
      primary: accent,
      secondary: amber,
      error: danger,
      surface: voidBlack,
      onSurface: text,
    );
    return ThemeData(
      useMaterial3: true,
      brightness: Brightness.dark,
      colorScheme: colors,
      scaffoldBackgroundColor: voidBlack,
      splashFactory: NoSplash.splashFactory,
      textTheme: const TextTheme(
        displaySmall: TextStyle(
          color: text,
          fontSize: 34,
          fontWeight: FontWeight.w300,
          letterSpacing: 5.2,
          height: 1.05,
        ),
        headlineSmall: TextStyle(
          color: text,
          fontSize: 20,
          fontWeight: FontWeight.w500,
          letterSpacing: 2.4,
        ),
        titleLarge: TextStyle(
          color: text,
          fontSize: 16,
          fontWeight: FontWeight.w600,
          letterSpacing: 2.1,
        ),
        titleMedium: TextStyle(
          color: text,
          fontSize: 13,
          fontWeight: FontWeight.w600,
          letterSpacing: 1.5,
        ),
        bodyMedium: TextStyle(
          color: textMuted,
          fontSize: 13,
          height: 1.45,
        ),
        labelLarge: TextStyle(
          color: text,
          fontSize: 12,
          fontWeight: FontWeight.w700,
          letterSpacing: 1.8,
        ),
      ),
      iconTheme: const IconThemeData(color: text, size: 20),
    );
  }
}

final class OdparPanel extends StatelessWidget {
  const OdparPanel({
    required this.child,
    this.padding = const EdgeInsets.all(16),
    this.borderRadius = const BorderRadius.all(Radius.circular(8)),
    super.key,
  });

  final Widget child;
  final EdgeInsets padding;
  final BorderRadius borderRadius;

  @override
  Widget build(BuildContext context) {
    return DecoratedBox(
      decoration: BoxDecoration(
        color: OdparDesign.panel,
        borderRadius: borderRadius,
        border: Border.all(color: OdparDesign.panelEdge),
      ),
      child: Padding(padding: padding, child: child),
    );
  }
}

final class OdparActionButton extends StatelessWidget {
  const OdparActionButton({
    required this.label,
    required this.onPressed,
    this.primary = false,
    super.key,
  });

  final String label;
  final VoidCallback onPressed;
  final bool primary;

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      height: 48,
      child: FilledButton(
        style: FilledButton.styleFrom(
          foregroundColor:
              primary ? OdparDesign.voidBlack : OdparDesign.text,
          backgroundColor:
              primary ? OdparDesign.accent : const Color(0xFF1B222A),
          shape: RoundedRectangleBorder(
            borderRadius: BorderRadius.circular(5),
            side: BorderSide(
              color: primary ? OdparDesign.accent : OdparDesign.panelEdge,
            ),
          ),
        ),
        onPressed: onPressed,
        child: Text(label),
      ),
    );
  }
}

final class OdparMetric extends StatelessWidget {
  const OdparMetric({
    required this.label,
    required this.value,
    this.emphasized = false,
    super.key,
  });

  final String label;
  final String value;
  final bool emphasized;

  @override
  Widget build(BuildContext context) {
    return Column(
      mainAxisSize: MainAxisSize.min,
      crossAxisAlignment: CrossAxisAlignment.start,
      children: <Widget>[
        Text(
          label,
          style: const TextStyle(
            color: OdparDesign.textMuted,
            fontSize: 9,
            fontWeight: FontWeight.w600,
            letterSpacing: 1.5,
          ),
        ),
        const SizedBox(height: 2),
        Text(
          value,
          style: TextStyle(
            color: emphasized ? OdparDesign.accent : OdparDesign.text,
            fontSize: 17,
            fontWeight: FontWeight.w500,
            letterSpacing: 0.4,
            fontFeatures: const <FontFeature>[FontFeature.tabularFigures()],
          ),
        ),
      ],
    );
  }
}
