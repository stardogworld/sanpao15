import type { BestMoveSummary, MoveOutcomeInfo } from "../analysis/bestMove";
import type { RecommendationModeView } from "./recommendationMode";

export interface MoveFunnelGroups {
  optimal: MoveOutcomeInfo[];
  acceptable: MoveOutcomeInfo[];
  mistake: MoveOutcomeInfo[];
}

function segment(className: string, count: number, total: number): HTMLElement {
  const element = document.createElement("span");
  element.className = `move-funnel-segment ${className}`;
  element.style.flexGrow = String(Math.max(count, total > 0 ? 0.25 : 0));
  element.title = `${count} / ${total}`;
  return element;
}

export function renderMoveFunnel(summary: BestMoveSummary, groups: MoveFunnelGroups, mode: RecommendationModeView): HTMLElement {
  const container = document.createElement("div");
  container.className = "move-funnel";
  const total = Math.max(1, summary.legalMoveCount);
  const text = document.createElement("p");
  text.textContent = `从 ${summary.legalMoveCount} 个合法着法中，筛出 ${groups.optimal.length} 个最优着法；当前使用${mode.label}。`;
  const bar = document.createElement("div");
  bar.className = "move-funnel-bar";
  bar.append(
    segment("optimal", groups.optimal.length, total),
    segment("acceptable", groups.acceptable.length, total),
    segment("mistake", groups.mistake.length, total),
  );
  container.append(text, bar);
  return container;
}
