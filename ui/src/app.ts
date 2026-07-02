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
  type WdlLineExplorerResult,
} from "./tablebase/lineExplorer";
import {
  badge,
  classificationBadge,
  outcomeBadge,
  sideBadge,
  stopReasonBadge,
} from "./ui/badges";

interface HistoryEntry {
  position: Position;
  lastMove: Move | null;
}

const initialNotation = "SSSSS/SSSSS/SSSSS/...../.CCC. c";
const drawingFirstMove: Move = { from: 22, to: 12, capture: true, capturedSquare: 12 };

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
  private readonly headerStatusEl = document.createElement("div");
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
  private readonly feedbackEl = document.createElement("div");
  private readonly initialCardEl = document.createElement("div");
  private readonly helpEl = document.createElement("div");

  private resultTable: ResultTable | null = null;
  private tableLoadError: string | null = null;
  private tablebase: TablebaseDirectory | null = null;
  private tablebaseResult: TablebaseRecommendationResult | null = null;
  private lineResult: WdlLineExplorerResult | null = null;
  private activeLinePly = 0;
  private autoplayTimer: number | null = null;
  private spotlightMove: Move | null = null;

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
        <div class="header-status"></div>
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
          <details class="app-panel game-panel" open>
            <summary>Game</summary>
            <div class="status-panel">
            <div class="metric"><span>Turn</span></div>
            <div class="metric"><span>Soldiers</span></div>
            <div class="metric"><span>Terminal</span></div>
            </div>
            <div class="feedback"></div>
          </details>
          <details class="app-panel tablebase-panel" open>
            <summary>Tablebase</summary>
            <div class="panel-heading">
              <div class="tablebase-actions"></div>
            </div>
            <div class="tablebase-status"></div>
            <div class="tablebase-result"></div>
          </details>
          <details class="app-panel line-panel" open>
            <summary>Line Explorer</summary>
            <div class="panel-heading">
              <div class="line-actions"></div>
            </div>
            <p class="panel-note">WDL-only: no shortest win, fastest draw, DTW, or DTC is encoded.</p>
            <div class="playback-actions"></div>
            <div class="playback-status"></div>
            <div class="line-result"></div>
          </details>
          <details class="app-panel initial-panel" open>
            <summary>Initial Position</summary>
            <div class="initial-card"></div>
          </details>
          <details class="app-panel help-panel">
            <summary>Help</summary>
            <div class="help-copy"></div>
          </details>
          <details class="app-panel analysis-panel">
            <summary>Reachable Table Analysis</summary>
          </details>
        </aside>
      </main>
    `;

    const headerStatus = this.requireElement(".header-status");
    const boardWrap = this.requireElement(".board-wrap");
    const toolbar = this.requireElement(".toolbar");
    const notationPanel = this.requireElement(".notation-panel");
    const notationActions = this.requireElement(".notation-actions");
    const statusPanel = this.requireElement(".status-panel");
    const feedback = this.requireElement(".feedback");
    const analysisPanel = this.requireElement(".analysis-panel");
    const tablebaseActions = this.requireElement(".tablebase-actions");
    const tablebaseStatus = this.requireElement(".tablebase-status");
    const tablebaseResult = this.requireElement(".tablebase-result");
    const lineActions = this.requireElement(".line-actions");
    const playbackActions = this.requireElement(".playback-actions");
    const playbackStatus = this.requireElement(".playback-status");
    const lineResult = this.requireElement(".line-result");
    const initialCard = this.requireElement(".initial-card");
    const helpCopy = this.requireElement(".help-copy");

    this.headerStatusEl.className = "header-status-copy";
    headerStatus.append(this.headerStatusEl);

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

    this.feedbackEl.className = "feedback-copy";
    feedback.append(this.feedbackEl);

    this.initialCardEl.className = "initial-card-copy";
    initialCard.append(this.initialCardEl);

    this.helpEl.className = "help-copy-inner";
    helpCopy.append(this.helpEl);

    this.analysisEl.className = "analysis-copy";
    analysisPanel.append(this.analysisEl);

    this.renderInitialCard();
    this.renderHelp();
    this.renderTablebaseStatus();
    this.render();
    this.renderPlaybackStatus();
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
    this.showFeedback("Initial position restored.");
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
    this.lineResult = null;
    this.activeLinePly = 0;
    this.render();
    this.renderPlaybackStatus();
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
    this.lineResult = null;
    this.activeLinePly = 0;
    this.render();
    this.renderPlaybackStatus();
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
    this.lineResult = null;
    this.activeLinePly = 0;
    this.render();
    this.renderPlaybackStatus();
    void this.queryTablebaseIfLoaded();
  }

  private clearPositionDerivedOutput(): void {
    this.analysisEl.textContent = "";
    this.tablebaseResult = null;
    this.tablebaseResultEl.textContent = "";
    this.recommendedMoves = [];
    this.spotlightMove = null;
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
      this.showFeedback("Tablebase loaded.");
      await this.queryTablebase();
    } catch (error) {
      this.tablebase = null;
      this.tablebaseStatusEl.textContent = error instanceof Error ? error.message : String(error);
      this.render();
    }
  }

  private async selectTablebaseFiles(): Promise<void> {
    this.tablebaseStatusEl.textContent = "Selecting tablebase files...";
    try {
      this.tablebase = await openTablebaseFiles();
      this.renderTablebaseStatus();
      this.showFeedback("Tablebase files loaded.");
      await this.queryTablebase();
    } catch (error) {
      this.tablebase = null;
      this.tablebaseStatusEl.textContent = error instanceof Error ? error.message : String(error);
      this.render();
    }
  }

  private renderTablebaseStatus(): void {
    if (!this.tablebase) {
      this.tablebaseStatusEl.replaceChildren(this.renderEmptyState(
        "Tablebase not loaded",
        "Select a directory or layer files to query WDL outcomes. The browser reads only the needed .s15res bytes.",
      ));
      return;
    }
    const layers = Array.from(this.tablebase.layers.keys()).sort((left, right) => left - right);
    const encodings = Array.from(new Set(Array.from(this.tablebase.layers.values()).map((layer) => layer.encoding))).join(", ");
    const complete = layers.length === 16 && layers[0] === 0 && layers[layers.length - 1] === 15;
    const container = document.createElement("div");
    container.className = "status-grid";
    container.append(
      this.statusItem("Status", complete ? "Complete 0..15" : "Partial", complete ? "drawing" : "warning"),
      this.statusItem("Source", this.tablebase.sourceName),
      this.statusItem("Layers", layers.join(", ")),
      this.statusItem("Encoding", encodings || "unknown"),
      this.statusItem("Ruleset", `0x${this.tablebase.rulesetHash.toString(16).toUpperCase()}`),
      this.statusItem("Read mode", "random .s15res byte reads"),
    );
    this.tablebaseStatusEl.replaceChildren(container);
  }

  private async queryTablebaseIfLoaded(): Promise<void> {
    if (this.tablebase) {
      await this.queryTablebase();
    }
  }

  private async queryTablebase(): Promise<void> {
    if (!this.tablebase) {
      this.tablebaseResultEl.replaceChildren(this.renderEmptyState(
        "No lookup yet",
        "Load a tablebase first. Querying will not load full layers into memory.",
      ));
      this.render();
      return;
    }

    this.tablebaseResultEl.textContent = "Reading tablebase bytes...";
    try {
      const result = await recommendMoves(this.tablebase, this.position);
      this.tablebaseResult = result;
      this.recommendedMoves = result.recommendedMoves.map((move) => move.move);
      this.tablebaseResultEl.replaceChildren(this.renderTablebaseResult(result));
      this.showFeedback(`Lookup updated: ${result.outcome}.`);
      this.render();
    } catch (error) {
      this.tablebaseResult = null;
      this.recommendedMoves = [];
      this.tablebaseResultEl.textContent = error instanceof Error ? error.message : String(error);
      this.showFeedback("Tablebase lookup failed.");
      this.render();
    }
  }

  private renderTablebaseResult(result: TablebaseRecommendationResult): HTMLElement {
    const container = document.createElement("div");
    container.className = "tablebase-result-grid";

    const summary = document.createElement("div");
    summary.className = "tablebase-summary summary-band";
    summary.append(
      this.summaryStat("Outcome", outcomeBadge(result.outcome)),
      this.summaryStat("Soldiers", String(result.soldierCount)),
      this.summaryStat("Dense index", result.denseIndex.toString()),
      this.summaryStat("Legal moves", String(result.moves.length)),
      this.summaryStat("Recommended", String(result.recommendedMoves.length)),
    );
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
          item.className = "move-row";
          const moveMain = document.createElement("span");
          moveMain.className = "move-main";
          moveMain.textContent = moveText(move.move);
          const detail = document.createElement("span");
          detail.className = "move-detail";
          detail.textContent = `k=${move.successorSoldierCount} i=${move.successorIndex.toString()}`;
          item.append(moveMain, outcomeBadge(move.successorOutcome), classificationBadge(move.classification), detail);
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
    this.showFeedback(result.error ? result.error : `Line explored: ${result.stopReason}.`);
    this.renderPlaybackStatus();
    this.render();
  }

  private renderLineResult(result: WdlLineExplorerResult): HTMLElement {
    const container = document.createElement("div");
    container.className = "line-result-grid";

    const summary = document.createElement("div");
    summary.className = "line-summary summary-band";
    summary.append(
      this.summaryStat("Start", outcomeBadge(result.startOutcome)),
      this.summaryStat("Stop", stopReasonBadge(result.stopReason)),
      this.summaryStat("Cycle", result.cycleStartPly !== null ? String(result.cycleStartPly) : "none"),
      this.summaryStat(result.error ? "Error" : "Plies", result.error ?? String(result.plies.length)),
    );
    container.append(summary);

    if (result.stopReason === "maxPlies") {
      const note = document.createElement("p");
      note.className = "panel-note warning-note";
      note.textContent = "This is a WDL sample line, not shortest play.";
      container.append(note);
    }

    const list = document.createElement("ol");
    list.className = "line-list";
    for (const ply of result.plies) {
      const item = document.createElement("li");
      item.className = "line-ply-item";
      if (ply.ply === this.activeLinePly) {
        item.classList.add("active");
      }
      if (result.cycleStartPly === ply.ply) {
        item.classList.add("cycle-start");
      }
      const button = document.createElement("button");
      button.type = "button";
      button.className = "line-ply-button";
      button.append(
        this.lineCell("ply", String(ply.ply)),
        this.lineCell("side", sideLabel(ply.sideToMove)),
        this.lineCell("move", moveText(ply.chosenMove)),
        this.lineCell("successor", ply.successorOutcome),
        this.lineCell("class", ply.classification),
        this.lineCell("soldiers", `k=${ply.soldierCount}`),
      );
      button.addEventListener("click", () => this.jumpToLinePly(ply.ply));
      item.append(button);
      if (ply.alternatives.length > 1) {
        const details = document.createElement("details");
        details.className = "alternatives";
        const summaryText = document.createElement("summary");
        summaryText.textContent = `${ply.alternatives.length - 1} alternatives`;
        const altList = document.createElement("ul");
        for (const alternative of ply.alternatives.slice(1)) {
          const altItem = document.createElement("li");
          altItem.textContent = `${moveText(alternative.move)} -> ${alternative.successorOutcome} (${alternative.classification})`;
          altList.append(altItem);
        }
        details.append(summaryText, altList);
        item.append(details);
      }
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
    this.spotlightMove = ply.chosenMove;
    this.clearSelection();
    this.refreshLineListHighlight();
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
      this.showFeedback("Position pasted.");
    } catch (error) {
      this.showFeedback(error instanceof Error ? error.message : String(error), true);
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
      this.lineResult = null;
      this.activeLinePly = 0;
      this.showFeedback(`Moved ${moveText(move)}.`);
      this.render();
      this.renderPlaybackStatus();
      void this.queryTablebaseIfLoaded();
      return;
    }

    const ownPiece =
      this.position.side === "cannon"
        ? this.position.cannons.has(square)
        : this.position.soldiers.has(square);
    if (!ownPiece || terminalOutcome(this.position) !== "Unknown") {
      this.clearSelection();
      this.showFeedback(terminalOutcome(this.position) !== "Unknown" ? "Terminal position: no move can be made." : "Select a piece for the side to move.");
      this.render();
      return;
    }

    this.selectedSquare = square;
    this.selectedMoves = generateLegalMoves(this.position).filter((candidate) => candidate.from === square);
    this.showFeedback(this.selectedMoves.length === 0 ? "That piece has no legal move." : `${this.selectedMoves.length} legal target(s).`);
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
      lineMove: this.currentLineMove(),
      spotlightMove: this.spotlightMove,
      onSquareClick: (square) => this.onSquareClick(square),
    });

    this.turnEl.replaceChildren(sideBadge(this.position.side));
    this.soldierCountEl.textContent = String(this.position.soldiers.size);
    this.resultEl.replaceChildren(outcomeBadge(terminalOutcome(this.position)));
    this.notationEl.textContent = positionToNotation(this.position);
    this.renderHeaderStatus();
  }

  private renderHeaderStatus(): void {
    this.headerStatusEl.replaceChildren(
      badge("sanpao15-min-four-soldiers", "neutral"),
      outcomeBadge(this.tablebaseResult?.outcome ?? terminalOutcome(this.position)),
      badge(this.tablebase ? `tablebase ${this.tablebase.layers.size}/16` : "tablebase not loaded", this.tablebase ? "drawing" : "warning"),
    );
  }

  private renderEmptyState(title: string, detail: string): HTMLElement {
    const container = document.createElement("div");
    container.className = "empty-state";
    const heading = document.createElement("strong");
    heading.textContent = title;
    const copy = document.createElement("p");
    copy.textContent = detail;
    container.append(heading, copy);
    return container;
  }

  private statusItem(label: string, value: string, tone: Parameters<typeof badge>[1] = "neutral"): HTMLElement {
    const item = document.createElement("div");
    item.className = "status-item";
    const key = document.createElement("span");
    key.textContent = label;
    const valueEl = value.length < 24 ? badge(value, tone) : document.createElement("strong");
    valueEl.textContent = value;
    item.append(key, valueEl);
    return item;
  }

  private summaryStat(label: string, value: string | HTMLElement): HTMLElement {
    const item = document.createElement("div");
    item.className = "summary-stat";
    const key = document.createElement("span");
    key.textContent = label;
    const val = typeof value === "string" ? document.createElement("strong") : value;
    if (typeof value === "string") {
      val.textContent = value;
    }
    item.append(key, val);
    return item;
  }

  private lineCell(label: string, value: string): HTMLElement {
    const cell = document.createElement("span");
    cell.className = `line-cell ${label}`;
    const small = document.createElement("small");
    small.textContent = label;
    const strong = document.createElement("strong");
    strong.textContent = value;
    cell.append(small, strong);
    return cell;
  }

  private currentLineMove(): Move | null {
    if (!this.lineResult) return null;
    return this.lineResult.plies[this.activeLinePly]?.chosenMove ?? null;
  }

  private refreshLineListHighlight(): void {
    const items = this.lineResultEl.querySelectorAll(".line-ply-item");
    items.forEach((item, index) => {
      item.classList.toggle("active", index === this.activeLinePly);
    });
  }

  private showFeedback(message: string, important = false): void {
    this.feedbackEl.textContent = message;
    this.feedbackEl.classList.toggle("important", important);
  }

  private renderInitialCard(): void {
    const container = document.createElement("div");
    container.className = "initial-card-grid";
    const notation = document.createElement("code");
    notation.textContent = initialNotation;
    const meaning = document.createElement("p");
    meaning.textContent = "The standard initial position is Draw. Cannon has exactly one drawing first move; every other legal first move enters SoldierWin.";
    const actions = document.createElement("div");
    actions.className = "card-actions";
    actions.append(
      this.makeButton("Reset to initial", () => this.reset()),
      this.makeButton("Show drawing move", () => {
        this.spotlightMove = drawingFirstMove;
        this.showFeedback("Drawing first move highlighted: 22->12 captures 12.");
        this.render();
      }),
      this.makeButton("Explore drawing line", () => {
        this.reset();
        window.setTimeout(() => {
          void this.exploreLine();
        }, 0);
      }),
      this.makeButton("Copy position", () => {
        this.pasteInputEl.value = initialNotation;
        void navigator.clipboard?.writeText(initialNotation);
        this.showFeedback("Initial position copied.");
      }),
    );
    container.append(
      this.summaryStat("Position", notation),
      this.summaryStat("Result", outcomeBadge("Draw")),
      this.summaryStat("Only drawing first move", "22->12 captures 12"),
      meaning,
      actions,
    );
    this.initialCardEl.replaceChildren(container);
  }

  private renderHelp(): void {
    this.helpEl.innerHTML = `
      <ul class="help-list">
        <li>Cannons move one orthogonal step to an empty square.</li>
        <li>Cannons capture by jumping over one empty square to a soldier.</li>
        <li>Soldiers move one orthogonal step to an empty square and do not capture.</li>
        <li>soldierCount &lt; 4 is CannonWin.</li>
        <li>No cannon legal move is SoldierWin.</li>
        <li>The dense tablebase is outcome-only WDL. It has no DTW, DTC, or shortest-play data.</li>
      </ul>
    `;
  }
}
