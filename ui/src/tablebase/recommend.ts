import { applyMove, generateLegalMoves, type Move, type Outcome, type Position } from "../engine";
import { lookupOutcome, type TablebaseDirectory, type TablebaseLookupResult } from "./denseResult";

export type MoveClassification = "winning" | "drawing" | "losing";

export interface RecommendedMove {
  move: Move;
  successor: Position;
  successorSoldierCount: number;
  successorIndex: bigint;
  successorOutcome: Outcome;
  classification: MoveClassification;
}

export interface TablebaseRecommendationResult extends TablebaseLookupResult {
  moves: RecommendedMove[];
  recommendedMoves: RecommendedMove[];
}

function classificationRank(classification: MoveClassification): number {
  switch (classification) {
    case "winning":
      return 0;
    case "drawing":
      return 1;
    case "losing":
      return 2;
  }
}

function winForSide(side: Position["side"]): Outcome {
  return side === "cannon" ? "CannonWin" : "SoldierWin";
}

function opponentWinForSide(side: Position["side"]): Outcome {
  return side === "cannon" ? "SoldierWin" : "CannonWin";
}

function classify(sideToMove: Position["side"], outcome: Outcome): MoveClassification {
  if (outcome === winForSide(sideToMove)) {
    return "winning";
  }
  if (outcome === "Draw") {
    return "drawing";
  }
  return "losing";
}

function wdlRecommendationTier(side: Position["side"], currentOutcome: Outcome, successorOutcome: Outcome): number {
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
  return classificationRank(classify(side, successorOutcome));
}

function compareRecommended(side: Position["side"], currentOutcome: Outcome, left: RecommendedMove, right: RecommendedMove): number {
  const tier = wdlRecommendationTier(side, currentOutcome, left.successorOutcome) -
    wdlRecommendationTier(side, currentOutcome, right.successorOutcome);
  if (tier !== 0) return tier;
  if (left.move.capture !== right.move.capture) return left.move.capture ? -1 : 1;
  if (left.move.capture && left.successorSoldierCount !== right.successorSoldierCount) {
    return left.successorSoldierCount - right.successorSoldierCount;
  }
  if (left.move.from !== right.move.from) return left.move.from - right.move.from;
  if (left.move.to !== right.move.to) return left.move.to - right.move.to;
  if (left.move.capturedSquare !== right.move.capturedSquare) return left.move.capturedSquare - right.move.capturedSquare;
  return left.successorIndex < right.successorIndex ? -1 : left.successorIndex > right.successorIndex ? 1 : 0;
}

function chooseRecommended(side: Position["side"], currentOutcome: Outcome, moves: RecommendedMove[]): RecommendedMove[] {
  if (moves.length === 0) return [];
  const bestTier = wdlRecommendationTier(side, currentOutcome, moves[0].successorOutcome);
  return moves.filter((move) => wdlRecommendationTier(side, currentOutcome, move.successorOutcome) === bestTier);
}

export function moveText(move: Move): string {
  return `${move.from}->${move.to}${move.capture ? ` x${move.capturedSquare}` : ""}`;
}

export async function recommendMoves(
  tablebase: TablebaseDirectory,
  position: Position,
): Promise<TablebaseRecommendationResult> {
  const current = await lookupOutcome(tablebase, position);
  const moves: RecommendedMove[] = [];

  for (const move of generateLegalMoves(position)) {
    const successor = applyMove(position, move);
    const lookedUp = await lookupOutcome(tablebase, successor);
    moves.push({
      move,
      successor,
      successorSoldierCount: lookedUp.soldierCount,
      successorIndex: lookedUp.denseIndex,
      successorOutcome: lookedUp.outcome,
      classification: classify(position.side, lookedUp.outcome),
    });
  }

  moves.sort((left, right) => compareRecommended(position.side, current.outcome, left, right));

  return {
    ...current,
    moves,
    recommendedMoves: chooseRecommended(position.side, current.outcome, moves),
  };
}
