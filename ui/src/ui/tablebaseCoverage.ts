import type { MtdStatus, TablebaseStatus } from "../tablebase/provider";

function layerSet(layers: number[] | undefined): Set<number> {
  return new Set(layers ?? []);
}

function invalidSet(layers: Array<{ soldierCount: number }> | undefined): Set<number> {
  return new Set((layers ?? []).map((layer) => layer.soldierCount));
}

function row(label: string, loadedLayers: number[] | undefined, invalidLayers: Array<{ soldierCount: number }> | undefined): HTMLElement {
  const loaded = layerSet(loadedLayers);
  const invalid = invalidSet(invalidLayers);
  const element = document.createElement("div");
  element.className = "coverage-row";
  const title = document.createElement("strong");
  title.textContent = label;
  element.append(title);
  for (let layer = 0; layer <= 15; layer += 1) {
    const cell = document.createElement("span");
    const state = invalid.has(layer) ? "invalid" : loaded.has(layer) ? "loaded" : "missing";
    cell.className = `coverage-cell ${state}`;
    cell.textContent = String(layer);
    cell.title = `${label} layer ${layer}: ${state}`;
    element.append(cell);
  }
  return element;
}

function mtdRow(mtd: MtdStatus | undefined): HTMLElement {
  if (!mtd) {
    const element = document.createElement("div");
    element.className = "coverage-row unsupported";
    const title = document.createElement("strong");
    title.textContent = "MTD";
    const note = document.createElement("span");
    note.textContent = "浏览器文件模式不支持 MTD；请使用本地后端。";
    element.append(title, note);
    return element;
  }
  return row("MTD", mtd.loadedLayers, mtd.invalidLayers);
}

export function renderTablebaseCoverage(status: TablebaseStatus): HTMLElement {
  const container = document.createElement("div");
  container.className = "tablebase-coverage";
  container.append(
    row("WDL", status.loadedLayers, status.invalidLayers),
    mtdRow(status.mtd),
  );
  return container;
}
