import { renderBoard } from "./board";
import { analyzePositionWithTable, loadResultTable, type ResultTable } from "./analysis/table";
import {
  applyMove,
  clonePosition,
  generateLegalMoves,
  initialPosition,
  outcomeLabel,
  parsePositionNotation,
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
import {
  exploreWdlLine,
  linePlyMoveText,
  type WdlLineExplorerResult,
} from "./tablebase/lineExplorer";

interface HistoryEntry {
  position: Position;
  lastMove: Move | null;
}

export class Sanpao15App {
  private position: Position = initialPosition();
  private selectedSquare: number | null = null;
  private selectedMoves: Move[] = [];
  private undoStack: HistoryEntry[] = [];
  private redoStack: HistoryEntry[] = [];
  private lastMove: Move | null = null;
  private recommendedMoves: Move[] = [];

  private readonly root: HTMLElement;
  private readonly boardEl = document.createElement("section");
  private readonly turnEl = document.createElement("strong");
  private readonly soldierCountEl = document.createElement("strong");
  private readonly resultEl = document.createElement("strong");
  private readonly notationEl = document.createElement("code");
  private readonly pasteInputEl = document.createElement("input");
  private readonly analysisEl = document.createElement("div");
  private readonly tablebaseStatusEl = document.createElement("div");
  private readonly tablebaseResultEl = document.createElement("div");
  private readonly lineResultEl = document.createElement("div");
  private readonly maxPliesInputEl = document.createElement("input");
  private readonly playbackStatusEl = document.createElement("div");

  private resultTable: ResultTable | null = null;
  private tableLoadError: string | null = null;
  private tablebase: TablebaseDirectory | null = null;
  private tablebaseResult: TablebaseRecommendationResult | null = null;
  private lineResult: WdlLineExplorerResult | null = null;
  private activeLinePly = 0;
  private autoplayTimer: number | null = null;

  constructor(root: HTMLElement) {
    this.root = root;
  }

  mount(): void {
    this.root.className = "app-shell";
    this.root.innerHTML = `
      <header class="topbar">
        <div>
          <h1>Sanpao15</h1>
          <p>Outcome-only tablebase explorer</p>
        </div>
      </header>
      <main class="game-layout">
        <div class="board-column">
          <div class="board-wrap"></div>
          <nav class="toolbar" aria-label="Game actions"></nav>
          <section class="notation-panel">
            <div class="panel-heading compact">
              <h2>Position</h2>
              <div class="notation-actions"></div>
            </div>
          </section>
        </div>
        <aside class="side-column">
          <section class="status-panel">
            <div class="metric"><span>Turn</span></div>
            <div class="metric"><span>Soldiers</span></div>
            <div class="metric"><span>Terminal</span></div>
          </section>
          <section class="tablebase-panel">
            <div class="panel-heading">
              <h2>Tablebase</h2>
              <div class="tablebase-actions"></div>
            </div>
            <div class="tablebase-status"></div>
            <div class="tablebase-result"></div>
          </section>
          <section class="line-panel">
            <div class="panel-heading">
              <h2>Line Explorer</h2>
              <div class="line-actions"></div>
            </div>
            <p class="panel-note">WDL-only: no shortest win, fastest draw, DTW, or DTC is encoded.</p>
            <div class="playback-actions"></div>
            <div class="playback-status"></div>
            <div class="line-result"></div>
          </section>
          <section class="analysis-panel"></section>
        </aside>
      </main>
    `;

    const boardWrap = this.requireElement(".board-wrap");
    const toolbar = this.requireElement(".toolbar");
    const notationPanel = this.requireElement(".notation-panel");
    const notationActions = this.requireElement(".notation-actions");
    const statusPanel = this.requireElement(".status-panel");
    const analysisPanel = this.requireElement(".analysis-panel");
    const tablebaseActions = this.requireElement(".tablebase-actions");
    const tablebaseStatus = this.requireElement(".tablebase-status");
    const tablebaseResult = this.requireElement(".tablebase-result");
    const lineActions = this.requireElement(".line-actions");
    const playbackActions = this.requireElement(".playback-actions");
    const playbackStatus = this.requireElement(".playback-status");
    const lineResult = this.requireElement(".line-result");

    this.boardEl.className = "board";
    this.boardEl.setAttribute("aria-label", "5x5 board");
    boardWrap.append(this.boardEl);

    const metrics = Array.from(statusPanel.querySelectorAll(".metric"));
    metrics[0].append(this.turnEl);
    metrics[1].append(this.soldierCountEl);
    metrics[2].append(this.resultEl);

    toolbar.append(
      this.makeButton("Reset", () => this.reset()),
      this.makeButton("Undo", () => this.undo()),
      this.makeButton("Redo", () => this.redo()),
      this.makeButton("Analyze current", () => {
        void this.analyze();
      }),
    );

    this.notationEl.className = "notation-code";
    this.pasteInputEl.type = "text";
    this.pasteInputEl.className = "notation-input";
    this.pasteInputEl.placeholder = "Paste notation";
    notationActions.append(
      this.makeButton("Copy", () => {
        void this.copyNotation();
      }),
      this.makeButton("Paste", () => this.pasteNotationFromInput()),
    );
    notationPanel.append(this.notationEl, this.pasteInputEl);

    tablebaseActions.append(
      this.makeButton("Directory", () => {
        void this.selectTablebase();
      }),
      this.makeButton("Layer files", () => {
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

    this.maxPliesInputEl.type = "number";
    this.maxPliesInputEl.className = "max-plies-input";
    this.maxPliesInputEl.min = "1";
    this.maxPliesInputEl.max = "1000";
    this.maxPliesInputEl.step = "1";
    this.maxPliesInputEl.value = "100";
    this.maxPliesInputEl.setAttribute("aria-label", "Max plies");
    lineActions.append(
      this.maxPliesInputEl,
      this.makeButton("Explore WDL line", () => {
        void this.exploreLine();
      }),
    );
    playbackActions.append(
      this.makeButton("Previous", () => this.previousLinePly()),
      this.makeButton("Next", () => this.nextLinePly()),
      this.makeButton("Auto", () => this.toggleAutoplay()),
    );
    this.playbackStatusEl.className = "playback-copy";
    this.lineResultEl.className = "line-result-copy";
    playbackStatus.append(this.playbackStatusEl);
    lineResult.append(this.lineResultEl);

    this.analysisEl.className = "analysis-copy";
    analysisPanel.append(this.analysisEl);

    this.render();
  }

  private requireElement(selector: string): HTMLElement {
    const element = this.root.querySelector(selector);
    if (!(element instanceof HTMLElement)) {
      throw new Error(`UI mount failed: ${selector}`);
    }
    return element;
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
    this.stopAutoplay();
    this.position = initialPosition();
    this.undoStack = [];
    this.redoStack = [];
    this.lastMove = null;
    this.clearSelection();
    this.clearPositionDerivedOutput();
    this.render();
    void this.queryTablebaseIfLoaded();
  }

  private pushUndo(): void {
    this.undoStack.push({ position: clonePosition(this.position), lastMove: this.lastMove });
    this.redoStack = [];
  }

  private undo(): void {
    const previous = this.undoStack.pop();
    if (!previous) return;
    this.redoStack.push({ position: clonePosition(this.position), lastMove: this.lastMove });
    this.position = clonePosition(previous.position);
    this.lastMove = previous.lastMove;
    this.clearSelection();
    this.clearPositionDerivedOutput();
    this.render();
    void this.queryTablebaseIfLoaded();
  }

  private redo(): void {
    const next = this.redoStack.pop();
    if (!next) return;
    this.undoStack.push({ position: clonePosition(this.position), lastMove: this.lastMove });
    this.position = clonePosition(next.position);
    this.lastMove = next.lastMove;
    this.clearSelection();
    this.clearPositionDerivedOutput();
    this.render();
    void this.queryTablebaseIfLoaded();
  }

  private setPosition(position: Position, lastMove: Move | null, pushHistory: boolean): void {
    this.stopAutoplay();
    if (pushHistory) {
      this.pushUndo();
    }
    this.position = clonePosition(position);
    this.lastMove = lastMove;
    this.clearSelection();
    this.clearPositionDerivedOutput();
    this.render();
    void this.queryTablebaseIfLoaded();
  }

  private clearPositionDerivedOutput(): void {
    this.analysisEl.textContent = "";
    this.tablebaseResult = null;
    this.tablebaseResultEl.textContent = "";
    this.recommendedMoves = [];
  }

  private async analyze(): Promise<void> {
    const outcome = terminalOutcome(this.position);
    const legalMoves = generateLegalMoves(this.position);
    this.analysisEl.textContent = "Loading reachable result table...";

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

  private async queryTablebaseIfLoaded(): Promise<void> {
    if (this.tablebase) {
      await this.queryTablebase();
    }
  }

  private async queryTablebase(): Promise<void> {
    if (!this.tablebase) {
      this.tablebaseResultEl.textContent = "Select a tablebase directory or layer files first.";
      return;
    }

    this.tablebaseResultEl.textContent = "Reading tablebase bytes...";
    try {
      const result = await recommendMoves(this.tablebase, this.position);
      this.tablebaseResult = result;
      this.recommendedMoves = result.recommendedMoves.map((move) => move.move);
      this.tablebaseResultEl.replaceChildren(this.renderTablebaseResult(result));
      this.render();
    } catch (error) {
      this.tablebaseResult = null;
      this.recommendedMoves = [];
      this.tablebaseResultEl.textContent = error instanceof Error ? error.message : String(error);
      this.render();
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

  private async exploreLine(): Promise<void> {
    this.stopAutoplay();
    const maxPlies = Number(this.maxPliesInputEl.value);
    this.lineResultEl.textContent = "Exploring WDL line...";
    const result = await exploreWdlLine(this.tablebase, this.position, maxPlies);
    this.lineResult = result;
    this.activeLinePly = 0;
    this.lineResultEl.replaceChildren(this.renderLineResult(result));
    this.renderPlaybackStatus();
  }

  private renderLineResult(result: WdlLineExplorerResult): HTMLElement {
    const container = document.createElement("div");
    container.className = "line-result-grid";

    const summary = document.createElement("div");
    summary.className = "line-summary";
    summary.textContent = [
      `Start outcome: ${outcomeLabel(result.startOutcome)}`,
      `Stop reason: ${result.stopReason}`,
      result.cycleStartPly !== null ? `Cycle start ply: ${result.cycleStartPly}` : "Cycle start ply: none",
      result.error ? `Error: ${result.error}` : `Plies: ${result.plies.length}`,
    ].join("\n");
    container.append(summary);

    const list = document.createElement("ol");
    list.className = "line-list";
    for (const ply of result.plies) {
      const item = document.createElement("li");
      const button = document.createElement("button");
      button.type = "button";
      button.className = "line-ply-button";
      button.textContent = `${ply.ply}. ${sideLabel(ply.sideToMove)} ${outcomeLabel(ply.outcome)} ${linePlyMoveText(ply)}`;
      button.addEventListener("click", () => this.jumpToLinePly(ply.ply));
      item.append(button);
      list.append(item);
    }
    container.append(list);
    return container;
  }

  private jumpToLinePly(plyIndex: number): void {
    this.showLinePly(plyIndex, true);
  }

  private showLinePly(plyIndex: number, stopPlayback: boolean): void {
    if (!this.lineResult) return;
    const ply = this.lineResult.plies[plyIndex];
    if (!ply) return;
    if (stopPlayback) {
      this.stopAutoplay();
    }
    this.activeLinePly = plyIndex;
    this.position = clonePosition(ply.position);
    this.lastMove = plyIndex === 0 ? null : this.lineResult.plies[plyIndex - 1]?.chosenMove ?? null;
    this.clearSelection();
    this.render();
    this.renderPlaybackStatus();
    void this.queryTablebaseIfLoaded();
  }

  private previousLinePly(): void {
    if (!this.lineResult || this.lineResult.plies.length === 0) return;
    this.jumpToLinePly(Math.max(0, this.activeLinePly - 1));
  }

  private nextLinePly(): void {
    if (!this.lineResult || this.lineResult.plies.length === 0) return;
    this.jumpToLinePly(Math.min(this.lineResult.plies.length - 1, this.activeLinePly + 1));
  }

  private toggleAutoplay(): void {
    if (this.autoplayTimer !== null) {
      this.stopAutoplay();
      return;
    }
    if (!this.lineResult || this.lineResult.plies.length === 0) return;
    this.autoplayTimer = window.setInterval(() => {
      if (!this.lineResult) {
        this.stopAutoplay();
        return;
      }
      if (this.activeLinePly + 1 >= this.lineResult.plies.length) {
        this.stopAutoplay();
        return;
      }
      this.showLinePly(this.activeLinePly + 1, false);
    }, 700);
    this.renderPlaybackStatus();
  }

  private stopAutoplay(): void {
    if (this.autoplayTimer !== null) {
      window.clearInterval(this.autoplayTimer);
      this.autoplayTimer = null;
    }
    this.renderPlaybackStatus();
  }

  private renderPlaybackStatus(): void {
    if (!this.lineResult) {
      this.playbackStatusEl.textContent = "No line explored.";
      return;
    }
    this.playbackStatusEl.textContent = [
      `Active ply: ${this.activeLinePly}`,
      `Stop reason: ${this.lineResult.stopReason}`,
      this.autoplayTimer === null ? "Autoplay: off" : "Autoplay: on",
    ].join("\n");
  }

  private async copyNotation(): Promise<void> {
    const notation = positionToNotation(this.position);
    this.pasteInputEl.value = notation;
    try {
      await navigator.clipboard.writeText(notation);
    } catch {
      // Keeping the input populated is a sufficient fallback for non-secure origins.
    }
  }

  private pasteNotationFromInput(): void {
    try {
      const next = parsePositionNotation(this.pasteInputEl.value);
      this.setPosition(next, null, true);
    } catch (error) {
      this.analysisEl.textContent = error instanceof Error ? error.message : String(error);
    }
  }

  private onSquareClick(square: number): void {
    const move = this.selectedMoves.find((candidate) => candidate.to === square);
    if (move) {
      this.pushUndo();
      this.position = applyMove(this.position, move);
      this.lastMove = move;
      this.clearSelection();
      this.clearPositionDerivedOutput();
      this.render();
      void this.queryTablebaseIfLoaded();
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
      recommendedMoves: this.recommendedMoves,
      lastMove: this.lastMove,
      onSquareClick: (square) => this.onSquareClick(square),
    });

    this.turnEl.textContent = sideLabel(this.position.side);
    this.soldierCountEl.textContent = String(this.position.soldiers.size);
    this.resultEl.textContent = outcomeLabel(terminalOutcome(this.position));
    this.notationEl.textContent = positionToNotation(this.position);
  }
}
