import type { Position } from "../engine";
import { outcomeText, zh } from "../i18n/zh";
import type { BestMoveSummary, MoveOutcomeInfo } from "../analysis/bestMove";
import type { RecommendationModeView } from "./recommendationMode";

export interface KeyMetrics {
  primary: string;
  secondary?: string;
  tone: "draw" | "cannon" | "soldier" | "neutral" | "warning";
  proofLabel: string;
}

function outcomeTone(outcome: string | undefined): KeyMetrics["tone"] {
  if (outcome === "Draw") return "draw";
  if (outcome === "CannonWin") return "cannon";
  if (outcome === "SoldierWin") return "soldier";
  return "neutral";
}

function distanceText(distance: number | undefined, saturated: boolean | undefined): string {
  if (saturated) return "≥255 手";
  if (distance === undefined) return zh.metrics.unknownDistance;
  return zh.metrics.withinPlies(distance);
}

export function keyMetricsForPosition(
  position: Position,
  summary: BestMoveSummary | null,
  bestMove: MoveOutcomeInfo | null,
  mode: RecommendationModeView,
): KeyMetrics {
  if (!summary) {
    return {
      primary: zh.hud.pending,
      secondary: zh.hud.loadTablebase,
      tone: "warning",
      proofLabel: zh.hud.loadTablebase,
    };
  }

  const mtd = bestMove?.successorMtd;
  const outcome = bestMove?.successorOutcome ?? summary.outcome;
  if (mtd?.available) {
    if (outcome === "Draw") {
      const saved = mtd.soldierSaved ?? mtd.materialTarget;
      const captures = mtd.cannonMaxCaptures ??
        (saved === undefined ? undefined : Math.max(0, position.soldiers.size - saved));
      return {
        primary: saved === undefined ? outcomeText(outcome) : zh.metrics.savedSoldiers(saved),
        secondary: captures === undefined ? zh.metrics.unknownDistance : zh.metrics.cannonCanCapture(captures),
        tone: "draw",
        proofLabel: mode.isMtdExact ? zh.hud.proofFine : zh.hud.proofBasic,
      };
    }
    return {
      primary: outcomeText(outcome),
      secondary: distanceText(mtd.guaranteeDistance, mtd.saturated),
      tone: outcomeTone(outcome),
      proofLabel: mode.isMtdExact ? zh.hud.proofFine : zh.hud.proofBasic,
    };
  }

  return {
    primary: outcomeText(summary.outcome),
    secondary: zh.metrics.basicOutcome,
    tone: outcomeTone(summary.outcome),
    proofLabel: zh.hud.proofBasic,
  };
}
