import type { Position, Side } from "../engine";
import { clonePosition } from "../engine";
import type { TablebaseDirectory } from "../tablebase/denseResult";
import { analyzeBestMoves, type BestMoveSummary } from "./bestMove";
import type { PositionValidation } from "../editor/validation";

export interface SideComparisonEntry {
  side: Side;
  validation: PositionValidation;
  summary: BestMoveSummary | null;
  error?: string;
}

export interface SideComparisonResult {
  cannon: SideComparisonEntry;
  soldier: SideComparisonEntry;
}

function withSide(position: Position, side: Side): Position {
  const next = clonePosition(position);
  next.side = side;
  return next;
}

export async function compareSides(
  tablebase: TablebaseDirectory | null,
  position: Position,
): Promise<SideComparisonResult> {
  const cannon = await analyzeBestMoves(tablebase, withSide(position, "cannon"));
  const soldier = await analyzeBestMoves(tablebase, withSide(position, "soldier"));
  return {
    cannon: {
      side: "cannon",
      validation: cannon.validation,
      summary: cannon.summary,
      error: cannon.error,
    },
    soldier: {
      side: "soldier",
      validation: soldier.validation,
      summary: soldier.summary,
      error: soldier.error,
    },
  };
}
