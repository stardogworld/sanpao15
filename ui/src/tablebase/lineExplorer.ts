import {
  applyMove,
  packPositionKey,
  terminalOutcome,
  type Move,
  type Outcome,
  type Position,
} from "../engine";
import {
  moveText,
  recommendMoves,
  type MoveClassification,
  type RecommendedMove,
} from "./recommend";
import type { TablebaseDirectory } from "./denseResult";

export interface WdlLineAlternative {
  move: Move;
  successorOutcome: Outcome;
  classification: MoveClassification;
}

export interface WdlLinePly {
  ply: number;
  position: Position;
  soldierCount: number;
  denseIndex: bigint;
  outcome: Outcome;
  sideToMove: Position["side"];
  chosenMove: Move;
  successor: Position;
  successorOutcome: Outcome;
  classification: MoveClassification;
  alternatives: WdlLineAlternative[];
}

export interface WdlLineExplorerResult {
  start: Position;
  startOutcome: Outcome;
  plies: WdlLinePly[];
  stopReason: "terminal" | "cycle" | "maxPlies" | "noLegalMoves" | "missingTablebase" | "lookupError";
  cycleStartPly: number | null;
  error?: string;
}

function winForSide(side: Position["side"]): Outcome {
  return side === "cannon" ? "CannonWin" : "SoldierWin";
}

function opponentWinForSide(side: Position["side"]): Outcome {
  return side === "cannon" ? "SoldierWin" : "CannonWin";
}

function wdlLineTier(side: Position["side"], currentOutcome: Outcome, successorOutcome: Outcome): number {
  if (currentOutcome === winForSide(side)) {
    if (successorOutcome === winForSide(side)) return 0;
    if (successorOutcome === "Draw") return 1;
    return 2;
  }
  if (currentOutcome === "Draw") {
    if (successorOutcome === "Draw") return 0;
    if (successorOutcome === winForSide(side)) return 1;
    return 2;
  }
  if (currentOutcome === opponentWinForSide(side)) {
    if (successorOutcome === "Draw") return 0;
    if (successorOutcome === opponentWinForSide(side)) return 1;
    return 2;
  }
  if (successorOutcome === winForSide(side)) return 0;
  if (successorOutcome === "Draw") return 1;
  return 2;
}

function compareWdlMoves(side: Position["side"], currentOutcome: Outcome, left: RecommendedMove, right: RecommendedMove): number {
  const tier = wdlLineTier(side, currentOutcome, left.successorOutcome) - wdlLineTier(side, currentOutcome, right.successorOutcome);
  if (tier !== 0) return tier;
  if (left.move.capture !== right.move.capture) return left.move.capture ? -1 : 1;
  if (left.move.capture && left.successorSoldierCount !== right.successorSoldierCount) {
    return left.successorSoldierCount - right.successorSoldierCount;
  }
  if (left.move.from !== right.move.from) return left.move.from - right.move.from;
  if (left.move.to !== right.move.to) return left.move.to - right.move.to;
  if (left.move.capture !== right.move.capture) return left.move.capture ? -1 : 1;
  if (left.move.capturedSquare !== right.move.capturedSquare) return left.move.capturedSquare - right.move.capturedSquare;
  return left.successorIndex < right.successorIndex ? -1 : left.successorIndex > right.successorIndex ? 1 : 0;
}

function lookupErrorReason(error: unknown): WdlLineExplorerResult["stopReason"] {
  const message = error instanceof Error ? error.message : String(error);
  return message.includes("Missing layer") ? "missingTablebase" : "lookupError";
}

export async function exploreWdlLine(
  tablebase: TablebaseDirectory | null,
  start: Position,
  maxPlies: number,
): Promise<WdlLineExplorerResult> {
  const result: WdlLineExplorerResult = {
    start,
    startOutcome: "Unknown",
    plies: [],
    stopReason: "maxPlies",
    cycleStartPly: null,
  };

  if (!tablebase) {
    result.stopReason = "missingTablebase";
    result.error = "Select a tablebase directory or layer files first.";
    return result;
  }
  if (!Number.isInteger(maxPlies) || maxPlies <= 0) {
    result.stopReason = "lookupError";
    result.error = "Max plies must be a positive integer.";
    return result;
  }

  let current = start;
  const visited = new Map<bigint, number>();
  visited.set(packPositionKey(current), 0);

  for (let ply = 0; ply < maxPlies; ply += 1) {
    try {
      const currentTerminal = terminalOutcome(current);
      const recommendation = await recommendMoves(tablebase, current);
      if (ply === 0) {
        result.startOutcome = recommendation.outcome;
      }
      if (currentTerminal !== "Unknown") {
        result.stopReason = "terminal";
        return result;
      }
      if (recommendation.moves.length === 0) {
        result.stopReason = "noLegalMoves";
        return result;
      }

      const sorted = [...recommendation.moves].sort((left, right) =>
        compareWdlMoves(current.side, recommendation.outcome, left, right),
      );
      const chosen = sorted[0];
      result.plies.push({
        ply,
        position: current,
        soldierCount: recommendation.soldierCount,
        denseIndex: recommendation.denseIndex,
        outcome: recommendation.outcome,
        sideToMove: current.side,
        chosenMove: chosen.move,
        successor: chosen.successor,
        successorOutcome: chosen.successorOutcome,
        classification: chosen.classification,
        alternatives: sorted.map((move) => ({
          move: move.move,
          successorOutcome: move.successorOutcome,
          classification: move.classification,
        })),
      });

      current = applyMove(current, chosen.move);
      const key = packPositionKey(current);
      const seenPly = visited.get(key);
      if (seenPly !== undefined) {
        result.stopReason = "cycle";
        result.cycleStartPly = seenPly;
        return result;
      }
      visited.set(key, ply + 1);
    } catch (error) {
      result.stopReason = lookupErrorReason(error);
      result.error = error instanceof Error ? error.message : String(error);
      return result;
    }
  }

  return result;
}

export function linePlyMoveText(ply: WdlLinePly): string {
  return `${moveText(ply.chosenMove)} -> ${ply.successorOutcome} (${ply.classification})`;
}
