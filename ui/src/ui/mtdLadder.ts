import { outcomeText } from "../i18n/zh";
import type { Outcome } from "../engine";
import type { MtdInfo } from "../tablebase/recommend";

function item(label: string, value: string): HTMLElement {
  const element = document.createElement("span");
  element.className = "mtd-ladder-item";
  const key = document.createElement("small");
  key.textContent = label;
  const strong = document.createElement("strong");
  strong.textContent = value;
  element.append(key, strong);
  return element;
}

function valueOrDash(value: number | undefined): string {
  return value === undefined ? "-" : String(value);
}

export function renderMtdLadder(currentSoldierCount: number, outcome: Outcome, mtd: MtdInfo | undefined): HTMLElement {
  const container = document.createElement("div");
  container.className = "mtd-ladder";
  if (!mtd?.available) {
    container.classList.add("missing");
    container.textContent = "MTD 数据不可用，当前仅按 WDL 结果推荐。";
    return container;
  }

  const target = outcome === "Draw"
    ? valueOrDash(mtd.soldierSaved ?? mtd.materialTarget)
    : outcomeText(outcome);
  const captures = valueOrDash(
    mtd.cannonMaxCaptures ??
      (mtd.soldierSaved === undefined ? undefined : Math.max(0, currentSoldierCount - mtd.soldierSaved)),
  );
  container.append(
    item("当前兵数", String(currentSoldierCount)),
    item(outcome === "Draw" ? "和棋保兵" : "目标", target),
    item("炮最多再吃", captures),
    item("保证步数", mtd.guaranteeDistance === undefined ? "-" : `${mtd.guaranteeDistance} ply`),
  );
  return container;
}
