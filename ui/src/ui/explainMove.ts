import type { Side } from "../engine";
import { formatMove, outcomeText, sideText } from "../i18n/zh";
import type { MoveOutcomeInfo } from "../analysis/bestMove";
import type { MtdInfo } from "../tablebase/recommend";
import type { RecommendationModeView } from "./recommendationMode";

export interface MoveExplanation {
  headline: string;
  detail: string;
  mtdDetail?: string;
  notation: string;
}

function formatPly(value: number | undefined): string {
  return value === undefined ? "未知 ply" : `${value} ply`;
}

function drawMtdDetail(currentSoldierCount: number, mtd: MtdInfo): string {
  const saved = mtd.soldierSaved ?? mtd.materialTarget;
  const captures = mtd.cannonMaxCaptures ?? (saved === undefined ? undefined : Math.max(0, currentSoldierCount - saved));
  const captureText = captures === undefined ? "未知数量" : String(captures);
  const savedText = saved === undefined ? "未知数量" : String(saved);
  return `炮方最多还能再吃 ${captureText} 个兵；兵方最优能保住 ${savedText} 兵。达到这个材料目标：${formatPly(mtd.guaranteeDistance)}。`;
}

function mtdDetailForOutcome(currentSoldierCount: number, move: MoveOutcomeInfo): string | undefined {
  const mtd = move.successorMtd;
  if (!mtd?.available) return undefined;
  if (move.successorOutcome === "Draw") return drawMtdDetail(currentSoldierCount, mtd);
  if (move.successorOutcome === "CannonWin") {
    return `炮方可保证 ${formatPly(mtd.guaranteeDistance)} 内吃到少于 4 兵。`;
  }
  if (move.successorOutcome === "SoldierWin") {
    return `兵方可保证 ${formatPly(mtd.guaranteeDistance)} 内围死炮。`;
  }
  return undefined;
}

function drawDetail(side: Side): string {
  return side === "cannon"
    ? "这步保持和棋，并在和棋范围内尽量降低兵方最终能保住的兵数。"
    : "这步保持和棋，并在和棋范围内尽量让兵方保住更多兵。";
}

function wdlDetail(mode: RecommendationModeView, side: Side, move: MoveOutcomeInfo): string {
  if (mode.isMtdExact) return "";
  if (move.successorOutcome === "Draw") {
    return `${drawDetail(side)}当前没有可用的 MTD 细分，所以只按胜负和结果层级推荐。`;
  }
  return `当前没有可用的 MTD 细分，所以只确认这步进入${outcomeText(move.successorOutcome)}结果层级。`;
}

export function explainMove(
  side: Side,
  currentSoldierCount: number,
  move: MoveOutcomeInfo,
  mode: RecommendationModeView,
): MoveExplanation {
  const headline = `这步保持${outcomeText(move.successorOutcome)}`;
  const sidePrefix = `${sideText(side)}走 ${formatMove(move.move)}`;
  const detail = move.successorOutcome === "Draw"
    ? `${sidePrefix}，${drawDetail(side)}推荐着法已标在棋盘上。`
    : `${sidePrefix}，进入${outcomeText(move.successorOutcome)}。推荐着法已标在棋盘上。`;
  const mtdDetail = mtdDetailForOutcome(currentSoldierCount, move) ?? wdlDetail(mode, side, move);
  return {
    headline,
    detail,
    mtdDetail: mtdDetail || undefined,
    notation: `记法：${formatMove(move.move)}`,
  };
}
