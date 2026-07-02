import { renderBoard } from "./board";
import { analyzePositionWithTable, loadResultTable, type ResultTable } from "./analysis/table";
import {
  applyMove,
  clonePosition,
  generateLegalMoves,
  initialPosition,
  parsePositionNotation,
  positionToNotation,
  terminalOutcome,
  type Move,
  type Position,
} from "./engine";
import {
  classificationText,
  formatMove,
  localizeErrorMessage,
  outcomeText,
  sideText,
  stopReasonText,
  zh,
} from "./i18n/zh";
import {
  openTablebaseDirectory,
  openTablebaseFiles,
  type TablebaseDirectory,
} from "./tablebase/denseResult";
import {
  recommendMoves,
  type MoveClassification,
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
          <h1>${zh.appTitle}</h1>
          <p>${zh.appSubtitle}</p>
        </div>
        <div class="header-status"></div>
      </header>
      <main class="game-layout">
        <div class="board-column">
          <div class="board-wrap"></div>
          <nav class="toolbar" aria-label="对局操作"></nav>
          <section class="notation-panel">
            <div class="panel-heading compact">
              <h2>${zh.labels.position}</h2>
              <div class="notation-actions"></div>
            </div>
          </section>
        </div>
        <aside class="side-column">
          <details class="app-panel game-panel" open>
            <summary>${zh.panels.game}</summary>
            <div class="status-panel">
            <div class="metric"><span>${zh.labels.turn}</span></div>
            <div class="metric"><span>${zh.labels.soldiers}</span></div>
            <div class="metric"><span>${zh.labels.terminal}</span></div>
            </div>
            <div class="feedback"></div>
          </details>
          <details class="app-panel tablebase-panel" open>
            <summary>${zh.panels.tablebase}</summary>
            <div class="panel-heading">
              <div class="tablebase-actions"></div>
            </div>
            <div class="tablebase-status"></div>
            <div class="tablebase-result"></div>
          </details>
          <details class="app-panel line-panel" open>
            <summary>${zh.panels.lineExplorer}</summary>
            <div class="panel-heading">
              <div class="line-actions"></div>
            </div>
            <p class="panel-note">${zh.line.note}</p>
            <div class="playback-actions"></div>
            <div class="playback-status"></div>
            <div class="line-result"></div>
          </details>
          <details class="app-panel initial-panel" open>
            <summary>${zh.panels.initialPosition}</summary>
            <div class="initial-card"></div>
          </details>
          <details class="app-panel help-panel">
            <summary>${zh.panels.help}</summary>
            <div class="help-copy"></div>
          </details>
          <details class="app-panel analysis-panel">
            <summary>${zh.panels.reachableAnalysis}</summary>
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
    this.boardEl.setAttribute("aria-label", "5x5 棋盘");
    boardWrap.append(this.boardEl);

    const metrics = Array.from(statusPanel.querySelectorAll(".metric"));
    metrics[0].append(this.turnEl);
    metrics[1].append(this.soldierCountEl);
    metrics[2].append(this.resultEl);

    toolbar.append(
      this.makeButton(zh.actions.reset, () => this.reset()),
      this.makeButton(zh.actions.undo, () => this.undo()),
      this.makeButton(zh.actions.redo, () => this.redo()),
      this.makeButton(zh.actions.analyze, () => {
        void this.analyze();
      }),
    );

    this.notationEl.className = "notation-code";
    this.pasteInputEl.type = "text";
    this.pasteInputEl.className = "notation-input";
    this.pasteInputEl.placeholder = zh.placeholders.pasteNotation;
    notationActions.append(
      this.makeButton(zh.actions.copy, () => {
        void this.copyNotation();
      }),
      this.makeButton(zh.actions.paste, () => this.pasteNotationFromInput()),
    );
    notationPanel.append(this.notationEl, this.pasteInputEl);

    tablebaseActions.append(
      this.makeButton(zh.actions.directory, () => {
        void this.selectTablebase();
      }),
      this.makeButton(zh.actions.layerFiles, () => {
        void this.selectTablebaseFiles();
      }),
      this.makeButton(zh.actions.query, () => {
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
    this.maxPliesInputEl.setAttribute("aria-label", zh.labels.maxPlies);
    lineActions.append(
      this.maxPliesInputEl,
      this.makeButton(zh.actions.exploreLine, () => {
        void this.exploreLine();
      }),
    );
    playbackActions.append(
      this.makeButton(zh.actions.previous, () => this.previousLinePly()),
      this.makeButton(zh.actions.next, () => this.nextLinePly()),
      this.makeButton(zh.actions.auto, () => this.toggleAutoplay()),
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
    button.title = label;
    button.setAttribute("aria-label", label);
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
    this.showFeedback(zh.initial.restored);
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
    this.analysisEl.textContent = zh.analysis.loading;

    if (!this.resultTable && !this.tableLoadError) {
      try {
        this.resultTable = await loadResultTable();
      } catch (error) {
        this.tableLoadError = error instanceof Error ? error.message : String(error);
      }
    }

    if (!this.resultTable) {
      const fallback = outcome !== "Unknown" ? `\n${zh.labels.terminal}：${outcomeText(outcome)}` : "";
      this.analysisEl.textContent = `${this.tableLoadError ? localizeErrorMessage(this.tableLoadError) : zh.analysis.tableNotLoaded}${fallback}\n${zh.analysis.legalMoves}：${legalMoves.length}`;
      return;
    }

    const analysis = analyzePositionWithTable(this.position, this.resultTable);
    const bestMove = analysis.bestMove ? formatMove(analysis.bestMove) : zh.analysis.none;
    const tableType = this.resultTable.exact ? zh.analysis.exact : this.resultTable.truncated ? zh.analysis.truncated : zh.analysis.partial;
    const foundText = analysis.foundInTable ? zh.analysis.found : zh.analysis.notFound;
    const moveLines = analysis.legalMoves.map((item) => {
      const best = item.isBest ? `，${zh.analysis.suggested}` : "";
      const distance = item.distance >= 0 ? String(item.distance) : "-";
      return `${formatMove(item.move)}：${outcomeText(item.outcome)}，${zh.analysis.distance} ${distance}${best}`;
    });

    this.analysisEl.textContent = [
      zh.analysis.title,
      `${zh.analysis.table}：${tableType}`,
      `${zh.analysis.current}：${outcomeText(analysis.outcome)}（${foundText}）`,
      `${zh.analysis.distance}：${analysis.distance >= 0 ? analysis.distance : "-"}`,
      `${zh.analysis.suggestedMove}：${bestMove}`,
      "",
      `${zh.analysis.legalMoves}：`,
      ...(moveLines.length > 0 ? moveLines : [zh.analysis.none]),
    ].join("\n");
  }

  private async selectTablebase(): Promise<void> {
    this.tablebaseStatusEl.textContent = zh.tablebase.selectingDirectory;
    try {
      this.tablebase = await openTablebaseDirectory();
      this.renderTablebaseStatus();
      this.showFeedback(zh.tablebase.loaded);
      await this.queryTablebase();
    } catch (error) {
      this.tablebase = null;
      this.tablebaseStatusEl.textContent = localizeErrorMessage(error instanceof Error ? error.message : String(error));
      this.render();
    }
  }

  private async selectTablebaseFiles(): Promise<void> {
    this.tablebaseStatusEl.textContent = zh.tablebase.selectingFiles;
    try {
      this.tablebase = await openTablebaseFiles();
      this.renderTablebaseStatus();
      this.showFeedback(zh.tablebase.filesLoaded);
      await this.queryTablebase();
    } catch (error) {
      this.tablebase = null;
      this.tablebaseStatusEl.textContent = localizeErrorMessage(error instanceof Error ? error.message : String(error));
      this.render();
    }
  }

  private renderTablebaseStatus(): void {
    if (!this.tablebase) {
      this.tablebaseStatusEl.replaceChildren(this.renderEmptyState(
        zh.tablebase.notLoadedTitle,
        zh.tablebase.notLoadedDetail,
      ));
      return;
    }
    const layers = Array.from(this.tablebase.layers.keys()).sort((left, right) => left - right);
    const encodings = Array.from(new Set(Array.from(this.tablebase.layers.values()).map((layer) => layer.encoding))).join(", ");
    const complete = layers.length === 16 && layers[0] === 0 && layers[layers.length - 1] === 15;
    const container = document.createElement("div");
    container.className = "status-grid";
    container.append(
      this.statusItem(zh.labels.status, complete ? zh.tablebase.complete : zh.tablebase.partial, complete ? "drawing" : "warning"),
      this.statusItem(zh.labels.source, this.tablebase.sourceName),
      this.statusItem(zh.labels.layers, layers.join(", ")),
      this.statusItem(zh.labels.encoding, encodings || zh.outcome.Unknown),
      this.statusItem(zh.labels.ruleset, `0x${this.tablebase.rulesetHash.toString(16).toUpperCase()}`),
      this.statusItem(zh.labels.readMode, zh.tablebase.randomRead),
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
        zh.tablebase.noLookupTitle,
        zh.tablebase.noLookupDetail,
      ));
      this.render();
      return;
    }

    this.tablebaseResultEl.textContent = zh.tablebase.reading;
    try {
      const result = await recommendMoves(this.tablebase, this.position);
      this.tablebaseResult = result;
      this.recommendedMoves = result.recommendedMoves.map((move) => move.move);
      this.tablebaseResultEl.replaceChildren(this.renderTablebaseResult(result));
      this.showFeedback(zh.tablebase.lookupUpdated(result.outcome));
      this.render();
    } catch (error) {
      this.tablebaseResult = null;
      this.recommendedMoves = [];
      this.tablebaseResultEl.textContent = localizeErrorMessage(error instanceof Error ? error.message : String(error));
      this.showFeedback(zh.tablebase.lookupFailed);
      this.render();
    }
  }

  private renderTablebaseResult(result: TablebaseRecommendationResult): HTMLElement {
    const container = document.createElement("div");
    container.className = "tablebase-result-grid";

    const summary = document.createElement("div");
    summary.className = "tablebase-summary summary-band";
    summary.append(
      this.summaryStat(zh.labels.outcome, outcomeBadge(result.outcome)),
      this.summaryStat(zh.labels.soldiers, String(result.soldierCount)),
      this.summaryStat(zh.labels.denseIndex, result.denseIndex.toString()),
      this.summaryStat(zh.labels.legalMoves, String(result.moves.length)),
      this.summaryStat(zh.labels.recommended, String(result.recommendedMoves.length)),
    );
    container.append(summary);

    for (const group of ["winning", "drawing", "losing"] as MoveClassification[]) {
      const section = document.createElement("section");
      section.className = `move-group ${group}`;
      const title = document.createElement("h3");
      title.textContent = classificationText(group);
      section.append(title);

      const moves = result.moves.filter((move) => move.classification === group);
      if (moves.length === 0) {
        const empty = document.createElement("p");
        empty.className = "empty-group";
        empty.textContent = zh.labels.none;
        section.append(empty);
      } else {
        const list = document.createElement("ul");
        for (const move of moves) {
          const item = document.createElement("li");
          item.className = "move-row";
          const moveMain = document.createElement("span");
          moveMain.className = "move-main";
          moveMain.textContent = formatMove(move.move);
          const detail = document.createElement("span");
          detail.className = "move-detail";
          detail.textContent = `${zh.labels.soldiers}=${move.successorSoldierCount}，${zh.labels.denseIndex}=${move.successorIndex.toString()}`;
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
    this.lineResultEl.textContent = zh.line.exploring;
    const result = await exploreWdlLine(this.tablebase, this.position, maxPlies);
    this.lineResult = result;
    this.activeLinePly = 0;
    this.lineResultEl.replaceChildren(this.renderLineResult(result));
    this.showFeedback(result.error ? localizeErrorMessage(result.error) : zh.line.explored(result.stopReason));
    this.renderPlaybackStatus();
    this.render();
  }

  private renderLineResult(result: WdlLineExplorerResult): HTMLElement {
    const container = document.createElement("div");
    container.className = "line-result-grid";

    const summary = document.createElement("div");
    summary.className = "line-summary summary-band";
    summary.append(
      this.summaryStat(zh.labels.start, outcomeBadge(result.startOutcome)),
      this.summaryStat(zh.labels.stop, stopReasonBadge(result.stopReason)),
      this.summaryStat(zh.labels.cycle, result.cycleStartPly !== null ? String(result.cycleStartPly) : zh.labels.none),
      this.summaryStat(result.error ? zh.labels.error : zh.labels.plies, result.error ? localizeErrorMessage(result.error) : String(result.plies.length)),
    );
    container.append(summary);

    if (result.stopReason === "maxPlies") {
      const note = document.createElement("p");
      note.className = "panel-note warning-note";
      note.textContent = zh.line.maxPliesNote;
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
        this.lineCell("ply", zh.line.cells.ply, String(ply.ply)),
        this.lineCell("side", zh.line.cells.side, sideText(ply.sideToMove)),
        this.lineCell("move", zh.line.cells.move, formatMove(ply.chosenMove)),
        this.lineCell("successor", zh.line.cells.successor, outcomeText(ply.successorOutcome)),
        this.lineCell("class", zh.line.cells.class, classificationText(ply.classification)),
        this.lineCell("soldiers", zh.line.cells.soldiers, String(ply.soldierCount)),
      );
      button.addEventListener("click", () => this.jumpToLinePly(ply.ply));
      item.append(button);
      if (ply.alternatives.length > 1) {
        const details = document.createElement("details");
        details.className = "alternatives";
        const summaryText = document.createElement("summary");
        summaryText.textContent = zh.line.alternatives(ply.alternatives.length - 1);
        const altList = document.createElement("ul");
        for (const alternative of ply.alternatives.slice(1)) {
          const altItem = document.createElement("li");
          altItem.textContent = `${formatMove(alternative.move)} -> ${outcomeText(alternative.successorOutcome)}（${classificationText(alternative.classification)}）`;
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
      this.playbackStatusEl.textContent = zh.labels.noLine;
      return;
    }
    this.playbackStatusEl.textContent = [
      `${zh.labels.activePly}：${this.activeLinePly}`,
      `${zh.labels.stop}：${stopReasonText(this.lineResult.stopReason)}`,
      `${zh.labels.autoplay}：${this.autoplayTimer === null ? zh.labels.off : zh.labels.on}`,
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
    this.showFeedback(zh.feedback.copied);
  }

  private pasteNotationFromInput(): void {
    try {
      const next = parsePositionNotation(this.pasteInputEl.value);
      this.setPosition(next, null, true);
      this.showFeedback(zh.feedback.pasted);
    } catch (error) {
      this.showFeedback(localizeErrorMessage(error instanceof Error ? error.message : String(error)), true);
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
      this.showFeedback(zh.feedback.moved(formatMove(move)));
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
      this.showFeedback(terminalOutcome(this.position) !== "Unknown" ? zh.feedback.terminal : zh.feedback.selectOwnPiece);
      this.render();
      return;
    }

    this.selectedSquare = square;
    this.selectedMoves = generateLegalMoves(this.position).filter((candidate) => candidate.from === square);
    this.showFeedback(this.selectedMoves.length === 0 ? zh.feedback.noLegalMove : zh.feedback.legalTargets(this.selectedMoves.length));
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
      badge(zh.rulesetBadge, "neutral"),
      outcomeBadge(this.tablebaseResult?.outcome ?? terminalOutcome(this.position)),
      badge(this.tablebase ? zh.tablebase.loadedBadge(this.tablebase.layers.size) : zh.tablebase.notLoadedBadge, this.tablebase ? "drawing" : "warning"),
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

  private lineCell(className: string, label: string, value: string): HTMLElement {
    const cell = document.createElement("span");
    cell.className = `line-cell ${className}`;
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
    meaning.textContent = zh.initial.meaning;
    const actions = document.createElement("div");
    actions.className = "card-actions";
    actions.append(
      this.makeButton(zh.actions.resetInitial, () => this.reset()),
      this.makeButton(zh.actions.showDrawingMove, () => {
        this.spotlightMove = drawingFirstMove;
        this.showFeedback(zh.initial.highlighted);
        this.render();
      }),
      this.makeButton(zh.actions.exploreDrawingLine, () => {
        this.reset();
        window.setTimeout(() => {
          void this.exploreLine();
        }, 0);
      }),
      this.makeButton(zh.actions.copyInitial, () => {
        this.pasteInputEl.value = initialNotation;
        void navigator.clipboard?.writeText(initialNotation);
        this.showFeedback(zh.initial.copied);
      }),
    );
    container.append(
      this.summaryStat(zh.initial.position, notation),
      this.summaryStat(zh.labels.result, outcomeBadge("Draw")),
      this.summaryStat(zh.initial.onlyMove, zh.initial.onlyMoveText),
      meaning,
      actions,
    );
    this.initialCardEl.replaceChildren(container);
  }

  private renderHelp(): void {
    const list = document.createElement("ul");
    list.className = "help-list";
    for (const item of zh.help) {
      const li = document.createElement("li");
      li.textContent = item;
      list.append(li);
    }
    this.helpEl.replaceChildren(list);
  }
}
