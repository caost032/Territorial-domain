import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

import 'src/engine/game_runtime.dart';
import 'src/input/input_router.dart';
import 'src/ui/design_system.dart';
import 'src/ui/game_screen.dart';

Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  await SystemChrome.setEnabledSystemUIMode(SystemUiMode.immersiveSticky);
  await SystemChrome.setPreferredOrientations(<DeviceOrientation>[
    DeviceOrientation.portraitUp,
    DeviceOrientation.portraitDown,
    DeviceOrientation.landscapeLeft,
    DeviceOrientation.landscapeRight,
  ]);
  runApp(const TerritorialDomainApp());
}

final class TerritorialDomainApp extends StatefulWidget {
  const TerritorialDomainApp({super.key});

  @override
  State<TerritorialDomainApp> createState() => _TerritorialDomainAppState();
}

final class _TerritorialDomainAppState extends State<TerritorialDomainApp> {
  late final MultiTouchInputRouter _input;
  GameRuntime? _runtime;
  Object? _startupError;

  @override
  void initState() {
    super.initState();
    _input = MultiTouchInputRouter();
    try {
      _runtime = GameRuntime.open(_input);
    } on Object catch (error) {
      _startupError = error;
    }
  }

  @override
  void dispose() {
    _runtime?.dispose();
    _input.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'ODPAR Territorial Domain',
      debugShowCheckedModeBanner: false,
      theme: OdparDesign.theme,
      home: _runtime == null
          ? _StartupFailure(error: _startupError)
          : GameScreen(runtime: _runtime!, input: _input),
    );
  }
}

final class _StartupFailure extends StatelessWidget {
  const _StartupFailure({required this.error});

  final Object? error;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: SafeArea(
        child: Center(
          child: Padding(
            padding: const EdgeInsets.all(28),
            child: ConstrainedBox(
              constraints: const BoxConstraints(maxWidth: 520),
              child: OdparPanel(
                child: Column(
                  mainAxisSize: MainAxisSize.min,
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: <Widget>[
                    Text(
                      'NATIVE CORE OFFLINE',
                      style: Theme.of(context).textTheme.titleLarge,
                    ),
                    const SizedBox(height: 12),
                    const Text(
                      'La app no pudo validar ODG API 14 / FFI ABI 1. '
                      'Reinstala un paquete generado por el flujo oficial.',
                      style: TextStyle(color: OdparDesign.textMuted),
                    ),
                    const SizedBox(height: 16),
                    Text(
                      error?.toString() ?? 'Error nativo desconocido',
                      style: const TextStyle(
                        color: OdparDesign.danger,
                        fontFamily: 'monospace',
                        fontSize: 12,
                      ),
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
