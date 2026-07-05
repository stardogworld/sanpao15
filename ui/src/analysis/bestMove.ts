import type { Move, Outcome, Position, Side } from "../engine";
import type { TablebaseRecommendationResult } from "../tablebase/recommend";
import { type MoveClassification, type MoveScore, type MtdInfo, type RecommendedMove } from "../tablebase/recommend";
import { validatePosition, type PositionValidation } from "../editor/validation";
import type { TablebaseProvider } from "../tablebase/provider";

export interface MoveOutcomeInfo {
  move: Move;
  successor: Position;
  successorOutcome: Outcome;
  classification: MoveClassification;
  successorSoldierCount: number;
  successorIndex: bigint;
  isBest: boolean;
  rank?: number;
  reason?: string;
  successorMtd?: MtdInfo;
  score?: MoveScore;
}

export interface BestMoveSummary {
  side: Side;
  outcome: Outcome;
  denseIndex: bigint;
  soldierCount: number;
  legalMoveCount: number;
  bestMoves: MoveOutcomeInfo[];
  winningMoves: MoveOutcomeInfo[];
  drawingMoves: MoveOutcomeInfo[];
  losingMoves: MoveOutcomeInfo[];
  allMoves: MoveOutcomeInfo[];
}

function toMoveInfo(move: RecommendedMove, bestMoves: RecommendedMove[]): MoveOutcomeInfo {
  return {
    move: move.move,
    successor: move.successor,
    successorOutcome: move.successorOutcome,
    classification: move.classification,
    successorSoldierCount: move.successorSoldierCount,
    successorIndex: move.successorIndex,
    isBest: move.isOptimal ?? bestMoves.includes(move),
    rank: move.rank,
    reason: move.reason,
    successorMtd: move.successorMtd,
    score: move.score,
  };
}

export function summarizeRecommendation(result: TablebaseRecommendationResult, side: Side): BestMoveSummary {
  const allMoves = result.moves.map((move) => toMoveInfo(move, result.recommendedMoves));
  return {
    side,
    outcome: result.outcome,
    denseIndex: result.denseIndex,
    soldierCount: result.soldierCount,
    legalMoveCount: result.moves.length,
    bestMoves: allMoves.filter((move) => move.isBest),
    winningMoves: allMoves.filter((move) => move.classification === "winning"),
    drawingMoves: allMoves.filter((move) => move.classification === "drawing"),
    losingMoves: allMoves.filter((move) => move.classification === "losing"),
    allMoves,
  };
}

export interface PositionAnalysisResult {
  validation: PositionValidation;
  recommendation: TablebaseRecommendationResult | null;
  summary: BestMoveSummary | null;
  error?: string;
}

export async function analyzeBestMoves(
  tablebase: TablebaseProvider | null,
  position: Position,
): Promise<PositionAnalysisResult> {
  const validation = tablebase?.validate(position) ?? validatePosition(position, null);
  if (!validation.canQuery || !tablebase) {
    return { validation, recommendation: null, summary: null };
  }

  try {
    const recommendation = await tablebase.recommend(position);
    return {
      validation,
      recommendation,
      summary: summarizeRecommendation(recommendation, position.side),
    };
  } catch (error) {
    return {
      validation: { ...validation, canQuery: false, reason: error instanceof Error ? error.message : String(error) },
      recommendation: null,
      summary: null,
      error: error instanceof Error ? error.message : String(error),
    };
  }
}
