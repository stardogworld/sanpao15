import type { Outcome, Side } from "../engine";
import { classificationText, outcomeText, sideText, stopReasonText } from "../i18n/zh";
import type { MoveClassification } from "../tablebase/recommend";

export type BadgeTone =
  | "neutral"
  | "cannon"
  | "soldier"
  | "winning"
  | "drawing"
  | "losing"
  | "warning";

export function badge(label: string, tone: BadgeTone = "neutral"): HTMLSpanElement {
  const element = document.createElement("span");
  element.className = `badge ${tone}`;
  element.textContent = label;
  return element;
}

export function outcomeBadge(outcome: Outcome): HTMLSpanElement {
  const tone =
    outcome === "CannonWin"
      ? "cannon"
      : outcome === "SoldierWin"
        ? "soldier"
        : outcome === "Draw"
          ? "drawing"
          : "neutral";
  return badge(outcomeText(outcome), tone);
}

export function sideBadge(side: Side): HTMLSpanElement {
  return badge(sideText(side), side === "cannon" ? "cannon" : "soldier");
}

export function classificationBadge(classification: MoveClassification): HTMLSpanElement {
  return badge(classificationText(classification), classification);
}

export function stopReasonBadge(reason: string): HTMLSpanElement {
  const tone = reason === "cycle" || reason === "maxPlies" ? "warning" : "neutral";
  return badge(stopReasonText(reason), tone);
}
