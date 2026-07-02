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

function classify(sideToMove: Position["side"], outcome: Outcome): MoveClassification {
  if (outcome === winForSide(sideToMove)) {
    return "winning";
  }
  if (outcome === "Draw") {
    return "drawing";
  }
  return "losing";
}

function chooseRecommended(moves: RecommendedMove[]): RecommendedMove[] {
  for (const classification of ["winning", "drawing", "losing"] as const) {
    const selected = moves.filter((move) => move.classification === classification);
    if (selected.length > 0) {
      return selected;
    }
  }
  return [];
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

  moves.sort((left, right) => {
    const rank = classificationRank(left.classification) - classificationRank(right.classification);
    if (rank !== 0) return rank;
    if (left.move.from !== right.move.from) return left.move.from - right.move.from;
    if (left.move.to !== right.move.to) return left.move.to - right.move.to;
    return Number(left.successorIndex - right.successorIndex);
  });

  return {
    ...current,
    moves,
    recommendedMoves: chooseRecommended(moves),
  };
}
