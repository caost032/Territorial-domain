import '../native/odg_bindings.dart';

final class GameLeader {
  const GameLeader({
    required this.score,
    required this.nameCode,
    required this.isPlayer,
  });

  final int score;
  final int nameCode;
  final bool isPlayer;
}

final class GameSnapshot {
  const GameSnapshot({
    required this.tick,
    required this.width,
    required this.height,
    required this.aliveCount,
    required this.playerAlive,
    required this.territoryTotalCells,
    required this.territoryPermille,
    required this.trailActive,
    required this.matchOver,
    required this.winnerId,
    required this.deathReason,
    required this.ownedTurrets,
    required this.carryingTurret,
    required this.carriedTurretAmmo,
    required this.turretActionAvailable,
    required this.carryingAmmo,
    required this.carriedAmmo,
    required this.ammoReserve,
    required this.carryingChip,
    required this.hackActionAvailable,
    required this.dropActionAvailable,
    required this.nearbyTurretVisible,
    required this.nearbyTurretAmmo,
    required this.nearbyTurretMaxAmmo,
    required this.stateHash,
    required this.leaders,
  });

  const GameSnapshot.empty()
      : tick = 0,
        width = 0,
        height = 0,
        aliveCount = 10,
        playerAlive = true,
        territoryTotalCells = 1,
        territoryPermille = 0,
        trailActive = false,
        matchOver = false,
        winnerId = 0,
        deathReason = 0,
        ownedTurrets = 0,
        carryingTurret = false,
        carriedTurretAmmo = 0,
        turretActionAvailable = false,
        carryingAmmo = false,
        carriedAmmo = 0,
        ammoReserve = 0,
        carryingChip = false,
        hackActionAvailable = false,
        dropActionAvailable = false,
        nearbyTurretVisible = false,
        nearbyTurretAmmo = 0,
        nearbyTurretMaxAmmo = 0,
        stateHash = 0,
        leaders = const <GameLeader>[];

  factory GameSnapshot.fromNative(
    OdgGameStats stats,
    List<GameLeader> leaders,
  ) {
    return GameSnapshot(
      tick: stats.tick,
      width: stats.width,
      height: stats.height,
      aliveCount: stats.aliveCount,
      playerAlive: stats.playerAlive != 0,
      territoryTotalCells: stats.territoryTotalCells,
      territoryPermille: stats.territoryPermille,
      trailActive: stats.playerTrailActive != 0,
      matchOver: stats.matchOver != 0,
      winnerId: stats.winnerId,
      deathReason: stats.playerDeathReason,
      ownedTurrets: stats.playerOwnedTurrets,
      carryingTurret: stats.playerCarryingTurret != 0,
      carriedTurretAmmo: stats.carriedTurretAmmo,
      turretActionAvailable: stats.turretActionAvailable != 0,
      carryingAmmo: stats.playerCarryingAmmoCrate != 0,
      carriedAmmo: stats.playerCarriedAmmo,
      ammoReserve: stats.playerAmmoReserve,
      carryingChip: stats.playerCarryingChip != 0,
      hackActionAvailable: stats.hackActionAvailable != 0,
      dropActionAvailable: stats.dropActionAvailable != 0,
      nearbyTurretVisible: stats.nearbyOwnedTurretVisible != 0,
      nearbyTurretAmmo: stats.nearbyOwnedTurretAmmo,
      nearbyTurretMaxAmmo: stats.nearbyOwnedTurretMaxAmmo,
      stateHash: stats.deterministicStateHash,
      leaders: List<GameLeader>.unmodifiable(leaders),
    );
  }

  final int tick;
  final int width;
  final int height;
  final int aliveCount;
  final bool playerAlive;
  final int territoryTotalCells;
  final int territoryPermille;
  final bool trailActive;
  final bool matchOver;
  final int winnerId;
  final int deathReason;
  final int ownedTurrets;
  final bool carryingTurret;
  final int carriedTurretAmmo;
  final bool turretActionAvailable;
  final bool carryingAmmo;
  final int carriedAmmo;
  final int ammoReserve;
  final bool carryingChip;
  final bool hackActionAvailable;
  final bool dropActionAvailable;
  final bool nearbyTurretVisible;
  final int nearbyTurretAmmo;
  final int nearbyTurretMaxAmmo;
  final int stateHash;
  final List<GameLeader> leaders;

  double get territoryPercent => territoryPermille / 10;

  double leaderPercent(int score) {
    if (territoryTotalCells <= 0) return 0;
    return score * 100 / territoryTotalCells;
  }

  String get contextualActionLabel {
    if (hackActionAvailable) return 'REPROGRAMAR';
    if (carryingTurret) return 'COLOCAR';
    return 'TORRETA';
  }

  String get statusLabel {
    if (trailActive) return 'TRAZO EXPUESTO';
    if (carryingChip) return 'CHIP EN CUSTODIA';
    if (carryingAmmo) return '$carriedAmmo DE SUMINISTRO';
    if (ammoReserve > 0) return '$ammoReserve EN RESERVA';
    return '$ownedTurrets TORRETAS';
  }
}
