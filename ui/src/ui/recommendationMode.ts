import type { PositionValidation } from "../editor/validation";
import { zh } from "../i18n/zh";
import type { TablebaseStatus, TablebaseProvider } from "../tablebase/provider";
import type { TablebaseRecommendationResult } from "../tablebase/recommend";

export interface RecommendationModeView {
  mode: "mtd" | "wdl" | "none";
  label: string;
  tone: "drawing" | "warning" | "neutral";
  detail: string;
  isMtdExact: boolean;
}

export function recommendationModeView(
  tablebase: TablebaseProvider | null,
  status: TablebaseStatus | null,
  recommendation: TablebaseRecommendationResult | null,
  validation: PositionValidation,
): RecommendationModeView {
  if (!tablebase) {
    return {
      mode: "none",
      label: zh.mtd.noTablebase,
      tone: validation.canQuery ? "neutral" : "warning",
      detail: validation.reason ?? zh.tablebase.notLoadedDetail,
      isMtdExact: false,
    };
  }

  if (recommendation?.recommendationPolicy === "mtd" && recommendation.mtdScoringEnabled === true) {
    return {
      mode: "mtd",
      label: zh.mtd.exact,
      tone: "drawing",
      detail: zh.mtd.exactDetail,
      isMtdExact: true,
    };
  }

  if (tablebase.kind === "browser-files") {
    return {
      mode: "wdl",
      label: zh.mtd.wdlOnly,
      tone: "warning",
      detail: zh.mtd.browserFilesNote,
      isMtdExact: false,
    };
  }

  const mtdLoaded = status?.mtd?.loaded === true;
  const mtdComplete = status?.mtd?.complete === true;

  if (recommendation?.recommendationPolicy === "wdl") {
    if (mtdLoaded && !mtdComplete) {
      return {
        mode: "wdl",
        label: zh.mtd.partialFallback,
        tone: "warning",
        detail: recommendation.mtdScoringDisabledReason ?? zh.mtd.partialFallbackDetail,
        isMtdExact: false,
      };
    }

    return {
      mode: "wdl",
      label: zh.mtd.wdlOnly,
      tone: "warning",
      detail: recommendation.mtdScoringDisabledReason ?? (mtdLoaded ? zh.mtd.wdlOnlyDetail : zh.mtd.backendNoMtdNote),
      isMtdExact: false,
    };
  }

  if (mtdLoaded && !mtdComplete) {
    return {
      mode: "wdl",
      label: zh.mtd.partialFallback,
      tone: "warning",
      detail: zh.mtd.partialFallbackDetail,
      isMtdExact: false,
    };
  }

  return {
    mode: "wdl",
    label: zh.mtd.wdlOnly,
    tone: "warning",
    detail: mtdLoaded ? zh.mtd.wdlOnlyDetail : zh.mtd.backendNoMtdNote,
    isMtdExact: false,
  };
}
