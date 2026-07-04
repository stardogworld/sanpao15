import { zh } from "../i18n/zh";
import type { MtdInfo, MoveScore } from "../tablebase/recommend";

export function formatMtdBrief(mtd: MtdInfo | null | undefined): string {
  if (!mtd?.available) return zh.mtd.shortMissing;
  const target = mtd.materialTarget ?? "-";
  const distance = mtd.guaranteeDistance ?? "-";
  return `目标 ${target} 兵，保证 ${distance} ply`;
}

export function formatMoveMtdBrief(mtd: MtdInfo | null | undefined): string {
  if (!mtd?.available) return "MTD -";
  const target = mtd.materialTarget ?? "-";
  const distance = mtd.guaranteeDistance ?? "-";
  return `目标 ${target} 兵 / ${distance} ply`;
}

export function formatScoreBrief(score: MoveScore | undefined): string {
  if (!score) return "-";
  return [
    `wdlTier=${score.wdlTier}`,
    `primary=${score.primary ?? "-"}`,
    `secondary=${score.secondary ?? "-"}`,
    `tertiary=${score.tertiary ?? "-"}`,
  ].join(" | ");
}
