import { renderBoard } from "./board";
import { analyzePositionWithTable, loadResultTable, type ResultTable } from "./analysis/table";
import {
  applyMove,
  generateLegalMoves,
  initialPosition,
  outcomeLabel,
  positionToNotation,
  sideLabel,
  terminalOutcome,
  type Move,
  type Position,
} from "./engine";
import {
  openTablebaseDirectory,
  openTablebaseFiles,
  type TablebaseDirectory,
} from "./tablebase/denseResult";
import {
  moveText,
  recommendMoves,
  type MoveClassification,
  type RecommendedMove,
  type TablebaseRecommendationResult,
} from "./tablebase/recommend";

interface HistoryEntry {
  position: Position;
}

export class Sanpao15App {
  private position: Position = initialPosition();
  private selectedSquare: number | null = null;
  private selectedMoves: Move[] = [];
  private history: HistoryEntry[] = [];

  private readonly root: HTMLElement;
  private readonly boardEl = document.createElement("section");
  private readonly turnEl = document.createElement("strong");
  private readonly soldierCountEl = document.createElement("strong");
  private readonly resultEl = document.createElement("strong");
  private readonly notationEl = document.createElement("code");
  private readonly analysisEl = document.createElement("div");
  private readonly tablebaseStatusEl = document.createElement("div");
  private readonly tablebaseResultEl = document.createElement("div");
  private resultTable: ResultTable | null = null;
  private tableLoadError: string | null = null;
  private tablebase: TablebaseDirectory | null = null;

  constructor(root: HTMLElement) {
    this.root = root;
  }

  mount(): void {
    this.root.className = "app-shell";
    this.root.innerHTML = `
      <header class="topbar">
        <div>
          <h1>Sanpao15</h1>
        </div>
      </header>
      <div class="game-layout">
        <div class="board-wrap"></div>
        <aside class="status-panel">
          <div class="metric"><span>Turn</span></div>
          <div class="metric"><span>Soldiers</span></div>
          <div class="metric"><span>Terminal</span></div>
        </aside>
      </div>
      <nav class="toolbar" aria-label="Game actions"></nav>
      <section class="notation-panel">
        <span>Position notation</span>
      </section>
      <section class="tablebase-panel">
        <div class="panel-heading">
          <h2>Tablebase</h2>
          <div class="tablebase-actions"></div>
        </div>
        <div class="tablebase-status"></div>
        <div class="tablebase-result"></div>
      </section>
      <section class="analysis-panel"></section>
    `;

    const boardWrap = this.root.querySelector(".board-wrap");
    const statusPanel = this.root.querySelector(".status-panel");
    const toolbar = this.root.querySelector(".toolbar");
    const notationPanel = this.root.querySelector(".notation-panel");
    const analysisPanel = this.root.querySelector(".analysis-panel");
    const tablebaseActions = this.root.querySelector(".tablebase-actions");
    const tablebaseStatus = this.root.querySelector(".tablebase-status");
    const tablebaseResult = this.root.querySelector(".tablebase-result");

    if (
      !boardWrap ||
      !statusPanel ||
      !toolbar ||
      !notationPanel ||
      !analysisPanel ||
      !tablebaseActions ||
      !tablebaseStatus ||
      !tablebaseResult
    ) {
      throw new Error("UI mount failed");
    }

    this.boardEl.className = "board";
    this.boardEl.setAttribute("aria-label", "5x5 board");
    boardWrap.append(this.boardEl);

    const metrics = Array.from(statusPanel.querySelectorAll(".metric"));
    metrics[0].append(this.turnEl);
    metrics[1].append(this.soldierCountEl);
    metrics[2].append(this.resultEl);

    toolbar.append(
      this.makeButton("New game", () => this.reset()),
      this.makeButton("Undo", () => this.undo()),
      this.makeButton("Analyze current", () => this.analyze()),
    );

    tablebaseActions.append(
      this.makeButton("Select directory", () => {
        void this.selectTablebase();
      }),
      this.makeButton("Select layer files", () => {
        void this.selectTablebaseFiles();
      }),
      this.makeButton("Query", () => {
        void this.queryTablebase();
      }),
    );
    this.tablebaseStatusEl.className = "tablebase-status-copy";
    this.tablebaseResultEl.className = "tablebase-result-copy";
    tablebaseStatus.append(this.tablebaseStatusEl);
    tablebaseResult.append(this.tablebaseResultEl);

    notationPanel.append(this.notationEl);
    this.analysisEl.className = "analysis-copy";
    analysisPanel.append(this.analysisEl);

    this.render();
  }

  private makeButton(label: string, onClick: () => void): HTMLButtonElement {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "command-button";
    button.textContent = label;
    button.addEventListener("click", onClick);
    return button;
  }

  private reset(): void {
    this.position = initialPosition();
    this.history = [];
    this.clearSelection();
    this.analysisEl.textContent = "";
    this.tablebaseResultEl.textContent = "";
    this.render();
  }

  private undo(): void {
    const previous = this.history.pop();
    if (!previous) {
      return;
    }
    this.position = previous.position;
    this.clearSelection();
    this.analysisEl.textContent = "";
    this.tablebaseResultEl.textContent = "";
    this.render();
  }

  private async analyze(): Promise<void> {
    const outcome = terminalOutcome(this.position);
    const legalMoves = generateLegalMoves(this.position);
    this.analysisEl.textContent = "Loading result table...";

    if (!this.resultTable && !this.tableLoadError) {
      try {
        this.resultTable = await loadResultTable();
      } catch (error) {
        this.tableLoadError = error instanceof Error ? error.message : String(error);
      }
    }

    if (!this.resultTable) {
      const fallback = outcome !== "Unknown" ? `\nTerminal: ${outcomeLabel(outcome)}` : "";
      this.analysisEl.textContent = `${this.tableLoadError ?? "Result table is not loaded."}${fallback}\nLegal moves: ${legalMoves.length}`;
      return;
    }

    const analysis = analyzePositionWithTable(this.position, this.resultTable);
    const bestMove = analysis.bestMove ? `${analysis.bestMove.from} -> ${analysis.bestMove.to}` : "none";
    const tableType = this.resultTable.exact ? "exact" : this.resultTable.truncated ? "truncated" : "partial";
    const foundText = analysis.foundInTable ? "found" : "not found";
    const moveLines = analysis.legalMoves.map((item) => {
      const capture = item.move.capture ? ` x${item.move.capturedSquare}` : "";
      const best = item.isBest ? ", suggested" : "";
      const distance = item.distance >= 0 ? String(item.distance) : "-";
      return `${item.move.from} -> ${item.move.to}${capture}: ${outcomeLabel(item.outcome)}, distance ${distance}${best}`;
    });

    this.analysisEl.textContent = [
      "Reachable-table analysis",
      `Table: ${tableType}`,
      `Current: ${outcomeLabel(analysis.outcome)} (${foundText})`,
      `Distance: ${analysis.distance >= 0 ? analysis.distance : "-"}`,
      `Suggested move: ${bestMove}`,
      "",
      "Legal moves:",
      ...(moveLines.length > 0 ? moveLines : ["none"]),
    ].join("\n");
  }

  private async selectTablebase(): Promise<void> {
    this.tablebaseStatusEl.textContent = "Selecting tablebase...";
    try {
      this.tablebase = await openTablebaseDirectory();
      this.renderTablebaseStatus();
      await this.queryTablebase();
    } catch (error) {
      this.tablebase = null;
      this.tablebaseStatusEl.textContent = error instanceof Error ? error.message : String(error);
    }
  }

  private async selectTablebaseFiles(): Promise<void> {
    this.tablebaseStatusEl.textContent = "Selecting tablebase files...";
    try {
      this.tablebase = await openTablebaseFiles();
      this.renderTablebaseStatus();
      await this.queryTablebase();
    } catch (error) {
      this.tablebase = null;
      this.tablebaseStatusEl.textContent = error instanceof Error ? error.message : String(error);
    }
  }

  private renderTablebaseStatus(): void {
    if (!this.tablebase) {
      this.tablebaseStatusEl.textContent = "";
      return;
    }
    const layers = Array.from(this.tablebase.layers.keys()).sort((left, right) => left - right);
    this.tablebaseStatusEl.textContent = [
      `Loaded: ${this.tablebase.sourceName}`,
      `Layers: ${layers.join(", ")}`,
      "Lookup mode: random-read outcome bytes only",
    ].join("\n");
  }

  private async queryTablebase(): Promise<void> {
    if (!this.tablebase) {
      this.tablebaseResultEl.textContent = "Select a tablebase directory or layer files first.";
      return;
    }

    this.tablebaseResultEl.textContent = "Reading tablebase bytes...";
    try {
      const result = await recommendMoves(this.tablebase, this.position);
      this.tablebaseResultEl.replaceChildren(this.renderTablebaseResult(result));
    } catch (error) {
      this.tablebaseResultEl.textContent = error instanceof Error ? error.message : String(error);
    }
  }

  private renderTablebaseResult(result: TablebaseRecommendationResult): HTMLElement {
    const container = document.createElement("div");
    container.className = "tablebase-result-grid";

    const summary = document.createElement("div");
    summary.className = "tablebase-summary";
    summary.textContent = [
      `Outcome: ${outcomeLabel(result.outcome)}`,
      `Soldiers: ${result.soldierCount}`,
      `Dense index: ${result.denseIndex.toString()}`,
      `Legal moves: ${result.moves.length}`,
      `Recommended: ${result.recommendedMoves.length}`,
      "Outcome-only: no DTW or shortest-win distance",
    ].join("\n");
    container.append(summary);

    for (const group of ["winning", "drawing", "losing"] as MoveClassification[]) {
      const section = document.createElement("section");
      section.className = `move-group ${group}`;
      const title = document.createElement("h3");
      title.textContent = group;
      section.append(title);

      const moves = result.moves.filter((move) => move.classification === group);
      if (moves.length === 0) {
        const empty = document.createElement("p");
        empty.className = "empty-group";
        empty.textContent = "none";
        section.append(empty);
      } else {
        const list = document.createElement("ul");
        for (const move of moves) {
          const item = document.createElement("li");
          item.textContent = `${moveText(move.move)} -> ${outcomeLabel(move.successorOutcome)} (k=${move.successorSoldierCount}, i=${move.successorIndex.toString()})`;
          if (result.recommendedMoves.includes(move)) {
            item.classList.add("recommended");
          }
          list.append(item);
        }
        section.append(list);
      }
      container.append(section);
    }

    return container;
  }

  private onSquareClick(square: number): void {
    const move = this.selectedMoves.find((candidate) => candidate.to === square);
    if (move) {
      this.history.push({ position: this.position });
      this.position = applyMove(this.position, move);
      this.clearSelection();
      this.analysisEl.textContent = "";
      this.tablebaseResultEl.textContent = "";
      this.render();
      return;
    }

    const ownPiece =
      this.position.side === "cannon"
        ? this.position.cannons.has(square)
        : this.position.soldiers.has(square);
    if (!ownPiece || terminalOutcome(this.position) !== "Unknown") {
      this.clearSelection();
      this.render();
      return;
    }

    this.selectedSquare = square;
    this.selectedMoves = generateLegalMoves(this.position).filter((candidate) => candidate.from === square);
    this.render();
  }

  private clearSelection(): void {
    this.selectedSquare = null;
    this.selectedMoves = [];
  }

  private render(): void {
    renderBoard(this.boardEl, {
      position: this.position,
      selectedSquare: this.selectedSquare,
      legalMoves: this.selectedMoves,
      onSquareClick: (square) => this.onSquareClick(square),
    });

    this.turnEl.textContent = sideLabel(this.position.side);
    this.soldierCountEl.textContent = String(this.position.soldiers.size);
    this.resultEl.textContent = outcomeLabel(terminalOutcome(this.position));
    this.notationEl.textContent = positionToNotation(this.position);
  }
}
