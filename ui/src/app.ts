import { renderBoard, type TargetOutcomeLabel } from "./board";
import {
  applyMove,
  clonePosition,
  generateLegalMoves,
  initialPosition,
  positionToNotation,
  terminalOutcome,
  type Move,
  type Position,
  type Side,
} from "./engine";
import { analyzeBestMoves, type BestMoveSummary, type MoveOutcomeInfo } from "./analysis/bestMove";
import { compareSides, type SideComparisonResult } from "./analysis/sideComparison";
import {
  applyEditTool,
  clearCannons,
  clearSoldiers,
  emptyPosition,
  initialAfterDrawingMoveNotation,
  movePieceFreely,
  parseEditablePositionNotation,
  positionPresets,
  positionWithSide,
  randomLegalDensePosition,
  type BoardEditTool,
  type BoardMode,
} from "./editor/positionEditor";
import { validatePosition, type PositionValidation } from "./editor/validation";
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
} from "./tablebase/denseResult";
import { type TablebaseRecommendationResult } from "./tablebase/recommend";
import {
  type WdlLineExplorerResult,
} from "./tablebase/lineExplorer";
import {
  BrowserFileTablebaseProvider,
  detectBackendStatus,
  detectBackendProvider,
  type TablebaseProvider,
  type TablebaseStatus,
} from "./tablebase/provider";
import {
  badge,
  classificationBadge,
  outcomeBadge,
  sideBadge,
  stopReasonBadge,
} from "./ui/badges";
import { detailsBlock } from "./ui/details";
import { explainMove } from "./ui/explainMove";
import { formatMoveMtdBrief, formatMtdBrief, formatScoreBrief } from "./ui/formatMtd";
import { renderMoveFunnel } from "./ui/moveFunnel";
import { renderMtdLadder } from "./ui/mtdLadder";
import { recommendationModeView, type RecommendationModeView } from "./ui/recommendationMode";
import { renderTablebaseCoverage } from "./ui/tablebaseCoverage";

interface HistoryEntry {
  position: Position;
  lastMove: Move | null;
}

const initialNotation = "SSSSS/SSSSS/SSSSS/...../.CCC. c";
const drawingFirstMove: Move = { from: 22, to: 12, capture: true, capturedSquare: 12 };
const analysisDebounceMs = 160;

export class Sanpao15App {
  private position: Position = initialPosition();
  private mode: BoardMode = "analysis";
  private editTool: BoardEditTool = "select";
  private autoQuery = true;
  private selectedSquare: number | null = null;
  private editSelectedSquare: number | null = null;
  private selectedMoves: Move[] = [];
  private undoStack: HistoryEntry[] = [];
  private redoStack: HistoryEntry[] = [];
  private lastMove: Move | null = null;
  private targetOutcomes: TargetOutcomeLabel[] = [];
  private boardFlipped = false;
  private showSquareNumbers = true;
  private showMoveArrow = true;
  private selectedMoveForDisplay: Move | null = null;

  private readonly root: HTMLElement;
  private readonly boardEl = document.createElement("section");
  private readonly boardToolsEl = document.createElement("div");
  private readonly headerStatusEl = document.createElement("div");
  private readonly turnEl = document.createElement("strong");
  private readonly cannonCountEl = document.createElement("strong");
  private readonly soldierCountEl = document.createElement("strong");
  private readonly resultEl = document.createElement("strong");
  private readonly notationEl = document.createElement("code");
  private readonly pasteInputEl = document.createElement("input");
  private readonly modeToggleEl = document.createElement("div");
  private readonly editorToolsEl = document.createElement("div");
  private readonly summaryEl = document.createElement("div");
  private readonly bestMoveEl = document.createElement("div");
  private readonly sideComparisonEl = document.createElement("div");
  private readonly moveGroupsEl = document.createElement("div");
  private readonly tablebaseStatusEl = document.createElement("div");
  private readonly lineResultEl = document.createElement("div");
  private readonly maxPliesInputEl = document.createElement("input");
  private readonly playbackStatusEl = document.createElement("div");
  private readonly feedbackEl = document.createElement("div");
  private readonly initialCardEl = document.createElement("div");
  private readonly helpEl = document.createElement("div");
  private readonly autoQueryInputEl = document.createElement("input");

  private tablebase: TablebaseProvider | null = null;
  private tablebaseStatus: TablebaseStatus | null = null;
  private tablebaseResult: TablebaseRecommendationResult | null = null;
  private currentSummary: BestMoveSummary | null = null;
  private currentValidation: PositionValidation = validatePosition(this.position, null);
  private sideComparison: SideComparisonResult | null = null;
  private lineResult: WdlLineExplorerResult | null = null;
  private activeLinePly = 0;
  private autoplayTimer: number | null = null;
  private spotlightMove: Move | null = null;
  private hoveredMove: Move | null = null;
  private analysisTimer: number | null = null;
  private analysisRequestId = 0;

  constructor(root: HTMLElement) {
    this.root = root;
  }

  mount(): void {
    this.root.className = "app-shell";
    this.root.innerHTML = `
      <header class="topbar">
        <div>
          <h1>${zh.appTitle}</h1>
          <p>任意局面查表分析器</p>
        </div>
        <div class="header-status"></div>
      </header>
      <main class="game-layout">
        <div class="board-column">
          <div class="mode-toggle"></div>
          <div class="editor-tools"></div>
          <div class="board-tools"></div>
          <div class="board-wrap"></div>
          <nav class="toolbar" aria-label="对局操作"></nav>
          <section class="notation-panel">
            <div class="panel-heading compact">
              <h2>局面代码</h2>
              <div class="notation-actions"></div>
            </div>
            <p class="panel-note">S = 兵，C = 炮，c = 炮方走，s = 兵方走。</p>
          </section>
        </div>
        <aside class="analysis-column">
          <section class="primary-result-card game-panel">
            <h2>${zh.panels.currentPosition}</h2>
            <div class="status-panel">
              <div class="metric"><span>轮到</span></div>
              <div class="metric"><span>炮数</span></div>
              <div class="metric"><span>兵数</span></div>
              <div class="metric"><span>结果</span></div>
            </div>
            <div class="feedback"></div>
            <div class="position-summary"></div>
          </section>
          <section class="best-move-card best-move-panel">
            <h2>${zh.panels.bestMove}</h2>
            <div class="best-move-result"></div>
          </section>
          <details class="app-panel moves-panel">
            <summary class="moves-summary-title">${zh.panels.moveAnalysis}</summary>
            <div class="move-groups-result"></div>
          </details>
          <details class="app-panel tablebase-panel">
            <summary>${zh.panels.tablebaseAdvanced}</summary>
            <div class="panel-heading">
              <div class="tablebase-actions"></div>
            </div>
            <div class="auto-query-row"></div>
            <div class="tablebase-status"></div>
          </details>
          <details class="app-panel comparison-panel">
            <summary>${zh.panels.sideComparison}</summary>
            <div class="side-comparison-result"></div>
          </details>
          <details class="app-panel line-panel">
            <summary>${zh.panels.lineExplorer}</summary>
            <div class="panel-heading">
              <div class="line-actions"></div>
            </div>
            <p class="panel-note">${zh.line.note}</p>
            <div class="playback-actions"></div>
            <div class="playback-status"></div>
            <div class="line-result"></div>
          </details>
          <details class="app-panel initial-panel">
            <summary>${zh.panels.initialPosition}</summary>
            <div class="initial-card"></div>
          </details>
          <details class="app-panel help-panel">
            <summary>${zh.panels.help}</summary>
            <div class="help-copy"></div>
          </details>
        </aside>
      </main>
    `;

    this.headerStatusEl.className = "header-status-copy";
    this.requireElement(".header-status").append(this.headerStatusEl);

    this.modeToggleEl.className = "segmented-control";
    this.requireElement(".mode-toggle").append(this.modeToggleEl);
    this.editorToolsEl.className = "editor-tools-copy";
    this.requireElement(".editor-tools").append(this.editorToolsEl);
    this.boardToolsEl.className = "board-tools-copy";
    this.requireElement(".board-tools").append(this.boardToolsEl);

    this.boardEl.className = "board";
    this.boardEl.setAttribute("aria-label", "5x5 棋盘");
    this.requireElement(".board-wrap").append(this.boardEl);

    const metrics = Array.from(this.root.querySelectorAll(".metric"));
    metrics[0].append(this.turnEl);
    metrics[1].append(this.cannonCountEl);
    metrics[2].append(this.soldierCountEl);
    metrics[3].append(this.resultEl);

    this.renderBoardTools();
    this.renderToolbar();

    this.notationEl.className = "notation-code";
    this.pasteInputEl.type = "text";
    this.pasteInputEl.className = "notation-input";
    this.pasteInputEl.placeholder = "粘贴局面代码";
    this.requireElement(".notation-actions").append(
      this.makeButton("复制局面代码", () => void this.copyNotation()),
      this.makeButton("粘贴并应用", () => this.pasteNotationFromInput()),
    );
    this.requireElement(".notation-panel").append(this.notationEl, this.pasteInputEl);

    this.feedbackEl.className = "feedback-copy";
    this.requireElement(".feedback").append(this.feedbackEl);
    this.summaryEl.className = "position-summary-copy";
    this.requireElement(".position-summary").append(this.summaryEl);
    this.bestMoveEl.className = "best-move-copy";
    this.requireElement(".best-move-result").append(this.bestMoveEl);
    this.sideComparisonEl.className = "side-comparison-copy";
    this.requireElement(".side-comparison-result").append(this.sideComparisonEl);
    this.moveGroupsEl.className = "move-groups-copy";
    this.requireElement(".move-groups-result").append(this.moveGroupsEl);

    this.autoQueryInputEl.type = "checkbox";
    this.autoQueryInputEl.checked = this.autoQuery;
    this.autoQueryInputEl.addEventListener("change", () => {
      this.autoQuery = this.autoQueryInputEl.checked;
      this.showFeedback(this.autoQuery ? "自动查表已开启。" : "自动查表已关闭，当前结果可能不是最新。");
      if (this.autoQuery) {
        this.scheduleAnalysis();
      }
    });
    const autoLabel = document.createElement("label");
    autoLabel.className = "toggle-row";
    autoLabel.append(this.autoQueryInputEl, document.createTextNode("自动查表"));
    this.requireElement(".auto-query-row").append(autoLabel);

    this.requireElement(".tablebase-actions").append(
      this.makeButton(zh.actions.directory, () => void this.selectTablebase()),
      this.makeButton(zh.actions.layerFiles, () => void this.selectTablebaseFiles()),
      this.makeButton("查询当前局面", () => void this.refreshAnalysisNow()),
    );
    this.tablebaseStatusEl.className = "tablebase-status-copy";
    this.requireElement(".tablebase-status").append(this.tablebaseStatusEl);

    this.maxPliesInputEl.type = "number";
    this.maxPliesInputEl.className = "max-plies-input";
    this.maxPliesInputEl.min = "1";
    this.maxPliesInputEl.max = "1000";
    this.maxPliesInputEl.step = "1";
    this.maxPliesInputEl.value = "100";
    this.maxPliesInputEl.setAttribute("aria-label", zh.labels.maxPlies);
    this.requireElement(".line-actions").append(
      this.maxPliesInputEl,
      this.makeButton(zh.actions.exploreLine, () => void this.exploreLine()),
    );
    this.requireElement(".playback-actions").append(
      this.makeButton(zh.actions.previous, () => this.previousLinePly()),
      this.makeButton(zh.actions.next, () => this.nextLinePly()),
      this.makeButton(zh.actions.auto, () => this.toggleAutoplay()),
    );
    this.playbackStatusEl.className = "playback-copy";
    this.lineResultEl.className = "line-result-copy";
    this.requireElement(".playback-status").append(this.playbackStatusEl);
    this.requireElement(".line-result").append(this.lineResultEl);

    this.initialCardEl.className = "initial-card-copy";
    this.requireElement(".initial-card").append(this.initialCardEl);
    this.helpEl.className = "help-copy-inner";
    this.requireElement(".help-copy").append(this.helpEl);

    this.renderModeControls();
    this.renderInitialCard();
    this.renderHelp();
    this.renderTablebaseStatus();
    this.render();
    this.renderPlaybackStatus();
    this.scheduleAnalysis();
    void this.detectBackendOnStartup();
  }

  private requireElement(selector: string): HTMLElement {
    const element = this.root.querySelector(selector);
    if (!(element instanceof HTMLElement)) {
      throw new Error(`UI mount failed: ${selector}`);
    }
    return element;
  }

  private makeButton(
    label: string,
    onClick: () => void,
    options?: { disabled?: boolean; title?: string },
  ): HTMLButtonElement {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "command-button";
    button.textContent = label;
    button.title = options?.title ?? label;
    button.setAttribute("aria-label", label);
    button.disabled = options?.disabled ?? false;
    button.addEventListener("click", () => {
      if (button.disabled) return;
      onClick();
    });
    return button;
  }

  private makeSegment<T extends string>(label: string, value: T, current: T, onSelect: (value: T) => void): HTMLButtonElement {
    const button = document.createElement("button");
    button.type = "button";
    button.className = value === current ? "segment active" : "segment";
    button.textContent = label;
    button.addEventListener("click", () => onSelect(value));
    return button;
  }

  private currentRecommendationMode(): RecommendationModeView {
    return recommendationModeView(this.tablebase, this.tablebaseStatus, this.tablebaseResult, this.currentValidation);
  }

  private renderBoardTools(): void {
    this.boardToolsEl.replaceChildren(
      this.makeButton(this.boardFlipped ? zh.board.unflip : zh.board.flip, () => this.toggleBoardFlip()),
      this.makeButton(this.showSquareNumbers ? zh.board.hideNumbers : zh.board.showNumbers, () => this.toggleSquareNumbers()),
      this.makeButton(this.showMoveArrow ? zh.board.hideArrow : zh.board.showArrow, () => this.toggleMoveArrow()),
      badge(this.boardFlipped ? zh.board.soldierView : zh.board.cannonView, "neutral"),
    );
  }

  private renderToolbar(): void {
    const toolbar = this.requireElement(".toolbar");
    const canApplyBestMove = this.primaryRecommendedMove() !== null;
    const gameGroup = this.toolbarGroup("对局");
    gameGroup.append(
      this.makeButton(zh.actions.undo, () => this.undo()),
      this.makeButton(zh.actions.redo, () => this.redo()),
      this.makeButton("查询当前局面", () => void this.refreshAnalysisNow()),
      this.makeButton(zh.recommendation.executeMove, () => this.applyBestMove(), {
        disabled: !canApplyBestMove,
        title: canApplyBestMove ? zh.recommendation.executeMove : zh.recommendation.noLegalMove,
      }),
    );

    const positionGroup = this.toolbarGroup("局面");
    positionGroup.append(
      this.makeButton("恢复初始局面", () => this.reset()),
      this.makeButton("随机局面", () => this.setPosition(randomLegalDensePosition(), null, true, "已生成随机局面。")),
      this.makeButton("清空棋盘", () => this.setPosition(clonePosition(emptyPosition), null, true, "已清空棋盘。")),
    );

    toolbar.replaceChildren(gameGroup, positionGroup);
  }

  private toolbarGroup(title: string): HTMLElement {
    const group = document.createElement("div");
    group.className = "toolbar-group";
    const label = document.createElement("span");
    label.className = "toolbar-group-title";
    label.textContent = title;
    group.append(label);
    return group;
  }

  private toggleBoardFlip(): void {
    this.boardFlipped = !this.boardFlipped;
    this.showFeedback(this.boardFlipped ? "已切换为兵方视角。" : "已切换为炮方视角。");
    this.renderBoardTools();
    this.renderHeaderStatus();
    this.render();
  }

  private toggleSquareNumbers(): void {
    this.showSquareNumbers = !this.showSquareNumbers;
    this.renderBoardTools();
    this.renderHeaderStatus();
    this.render();
  }

  private toggleMoveArrow(): void {
    this.showMoveArrow = !this.showMoveArrow;
    this.renderBoardTools();
    this.renderSummaryPanels();
    this.render();
  }

  private renderModeControls(): void {
    this.modeToggleEl.replaceChildren(
      this.makeSegment("分析模式", "analysis", this.mode, (mode) => this.setMode(mode)),
      this.makeSegment("编辑模式", "edit", this.mode, (mode) => this.setMode(mode)),
      this.sideToggleButton("炮方走", "cannon"),
      this.sideToggleButton("兵方走", "soldier"),
    );

    const presetSelect = document.createElement("select");
    presetSelect.className = "preset-select";
    presetSelect.setAttribute("aria-label", "示例局面");
    const defaultOption = document.createElement("option");
    defaultOption.value = "";
    defaultOption.textContent = "示例局面";
    presetSelect.append(defaultOption);
    for (const preset of positionPresets()) {
      const option = document.createElement("option");
      option.value = preset.id;
      option.textContent = preset.label;
      presetSelect.append(option);
    }
    const randomOption = document.createElement("option");
    randomOption.value = "random";
    randomOption.textContent = "随机合法局面";
    presetSelect.append(randomOption);
    presetSelect.addEventListener("change", () => {
      const id = presetSelect.value;
      presetSelect.value = "";
      if (id === "random") {
        this.setPosition(randomLegalDensePosition(), null, true, "已应用随机合法局面。");
        return;
      }
      const preset = positionPresets().find((item) => item.id === id);
      if (preset) {
        this.setPosition(preset.position, null, true, `已应用示例局面：${preset.label}。`);
      }
    });

    this.editorToolsEl.replaceChildren(
      this.makeSegment("选择/移动", "select", this.editTool, (tool) => this.setEditTool(tool)),
      this.makeSegment("放炮", "placeCannon", this.editTool, (tool) => this.setEditTool(tool)),
      this.makeSegment("放兵", "placeSoldier", this.editTool, (tool) => this.setEditTool(tool)),
      this.makeSegment("删除", "erase", this.editTool, (tool) => this.setEditTool(tool)),
      this.makeButton("只清空兵", () => this.setPosition(clearSoldiers(this.position), null, true, "已清空兵。")),
      this.makeButton("只清空炮", () => this.setPosition(clearCannons(this.position), null, true, "已清空炮。")),
      presetSelect,
    );
    this.editorToolsEl.classList.toggle("hidden", this.mode !== "edit");
  }

  private sideToggleButton(label: string, side: Side): HTMLButtonElement {
    return this.makeSegment(label, side, this.position.side, (nextSide) => {
      this.setPosition(positionWithSide(this.position, nextSide), this.lastMove, true, `已切换为${sideText(nextSide)}走。`);
    });
  }

  private setMode(mode: BoardMode): void {
    this.mode = mode;
    this.clearSelection();
    this.editSelectedSquare = null;
    this.showFeedback(mode === "analysis" ? "已切换到分析模式。" : "已切换到编辑模式。");
    this.renderModeControls();
    this.render();
  }

  private setEditTool(tool: BoardEditTool): void {
    this.editTool = tool;
    this.editSelectedSquare = null;
    this.renderModeControls();
    this.render();
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
    this.renderModeControls();
    this.render();
    this.scheduleAnalysis();
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
    this.afterPositionChanged(null);
  }

  private redo(): void {
    const next = this.redoStack.pop();
    if (!next) return;
    this.undoStack.push({ position: clonePosition(this.position), lastMove: this.lastMove });
    this.position = clonePosition(next.position);
    this.lastMove = next.lastMove;
    this.afterPositionChanged(null);
  }

  private setPosition(position: Position, lastMove: Move | null, pushHistory: boolean, feedback?: string): void {
    this.stopAutoplay();
    if (pushHistory) {
      this.pushUndo();
    }
    this.position = clonePosition(position);
    this.lastMove = lastMove;
    this.afterPositionChanged(feedback ?? null);
  }

  private afterPositionChanged(feedback: string | null): void {
    this.clearSelection();
    this.editSelectedSquare = null;
    this.clearPositionDerivedOutput();
    this.lineResult = null;
    this.activeLinePly = 0;
    if (feedback) {
      this.showFeedback(feedback);
    }
    this.renderModeControls();
    this.render();
    this.renderPlaybackStatus();
    this.scheduleAnalysis();
  }

  private clearPositionDerivedOutput(): void {
    this.tablebaseResult = null;
    this.currentSummary = null;
    this.sideComparison = null;
    this.targetOutcomes = [];
    this.spotlightMove = null;
    this.hoveredMove = null;
    this.selectedMoveForDisplay = null;
  }

  private async detectBackendOnStartup(): Promise<void> {
    const provider = await detectBackendProvider();
    if (provider && !this.tablebase) {
      this.tablebase = provider;
      this.tablebaseStatus = await provider.status();
      this.showFeedback("本地后端已连接，表库已自动加载。");
      this.renderTablebaseStatus();
      await this.refreshAnalysisNow();
      return;
    }
    if (!this.tablebase) {
      this.tablebaseStatus = await detectBackendStatus();
      if (this.tablebaseStatus && !this.tablebaseStatus.complete) {
        this.renderTablebaseStatus();
      }
    }
  }

  private scheduleAnalysis(): void {
    this.currentValidation = this.tablebase?.validate(this.position) ?? validatePosition(this.position, null);
    this.renderSummaryPanels();
    if (!this.autoQuery) {
      this.bestMoveEl.replaceChildren(this.renderEmptyState("自动查表已关闭", "当前结果可能不是最新，请点击“查询当前局面”。"));
      return;
    }
    if (this.analysisTimer !== null) {
      window.clearTimeout(this.analysisTimer);
    }
    this.analysisTimer = window.setTimeout(() => {
      void this.refreshAnalysisNow();
    }, analysisDebounceMs);
  }

  private async refreshAnalysisNow(): Promise<void> {
    if (this.analysisTimer !== null) {
      window.clearTimeout(this.analysisTimer);
      this.analysisTimer = null;
    }
    const requestId = ++this.analysisRequestId;
    this.currentValidation = this.tablebase?.validate(this.position) ?? validatePosition(this.position, null);
    this.renderSummaryPanels();
    if (!this.currentValidation.canQuery) {
      this.tablebaseResult = null;
      this.currentSummary = null;
      this.sideComparison = null;
      this.targetOutcomes = [];
      this.renderSummaryPanels();
      this.render();
      return;
    }

    this.bestMoveEl.textContent = zh.tablebase.reading;
    const [analysis, comparison] = await Promise.all([
      this.tablebase?.analyze(this.position) ?? analyzeBestMoves(null, this.position),
      this.tablebase?.compareSides(this.position) ?? compareSides(null, this.position),
    ]);
    if (requestId !== this.analysisRequestId) {
      return;
    }

    this.currentValidation = analysis.validation;
    this.tablebaseResult = analysis.recommendation;
    this.currentSummary = analysis.summary;
    this.sideComparison = comparison;
    this.targetOutcomes = this.selectedMoves
      .map((move) => analysis.recommendation?.moves.find((item) => movesSame(item.move, move)))
      .filter((move): move is NonNullable<typeof move> => !!move)
      .map((move) => ({
        move: move.move,
        outcome: move.successorOutcome,
        classification: move.classification,
      }));
    this.render();
    this.renderSummaryPanels();
  }

  private renderSummaryPanels(): void {
    this.summaryEl.replaceChildren(this.renderPositionSummary());
    this.bestMoveEl.replaceChildren(this.renderBestMovePanel());
    this.sideComparisonEl.replaceChildren(this.renderSideComparisonPanel());
    this.moveGroupsEl.replaceChildren(this.renderMoveGroupsPanel());
    this.renderMoveGroupsSummaryTitle();
    this.renderTablebaseStatus();
    this.renderToolbar();
  }

  private renderMoveGroupsSummaryTitle(): void {
    const summary = this.root.querySelector(".moves-summary-title");
    if (!(summary instanceof HTMLElement)) return;
    if (!this.currentSummary) {
      summary.textContent = zh.moveGroups.title;
      return;
    }
    const bestTier = this.currentSummary.bestMoves[0]?.score?.wdlTier;
    const mistakeCount = this.currentSummary.allMoves.filter((move) =>
      !move.isBest &&
      (bestTier !== undefined ? move.score?.wdlTier !== bestTier : move.classification === "losing")).length;
    summary.textContent = `${zh.moveGroups.title}（总 ${this.currentSummary.legalMoveCount}，最优 ${this.currentSummary.bestMoves.length}，失误 ${mistakeCount}）`;
  }

  private primaryRecommendedMove(): Move | null {
    return this.tablebaseResult?.bestMove?.move
      ?? this.currentSummary?.bestMoves[0]?.move
      ?? null;
  }

  private bestMoveInfo(): MoveOutcomeInfo | null {
    const backendBest = this.tablebaseResult?.bestMove?.move;
    if (backendBest) {
      return this.currentSummary?.allMoves.find((move) => movesSame(move.move, backendBest)) ?? null;
    }
    return this.currentSummary?.bestMoves[0] ?? null;
  }

  private renderPositionSummary(): HTMLElement {
    const container = document.createElement("div");
    container.className = "position-summary-stack";
    const mode = this.currentRecommendationMode();
    const currentMtd = this.tablebaseResult?.currentMtd;
    const core = document.createElement("div");
    core.className = "status-grid core-status-grid";
    container.append(
      core,
    );
    core.append(
      this.statusItem(zh.labels.result, this.currentSummary?.outcome ? outcomeText(this.currentSummary.outcome) : outcomeText(terminalOutcome(this.position))),
      this.statusItem(zh.labels.turn, sideText(this.position.side), this.position.side),
      this.statusItem("炮数", String(this.position.cannons.size)),
      this.statusItem(zh.labels.soldiers, String(this.position.soldiers.size)),
      this.statusItem("推荐", mode.label, mode.tone),
      this.statusItem("MTD", currentMtd?.available ? zh.mtd.currentAvailable : this.currentMtdStatusText(), currentMtd?.available ? "drawing" : "warning"),
    );
    const note = document.createElement("p");
    note.className = currentMtd?.available ? "panel-note mtd-note" : "panel-note warning-note";
    note.textContent = currentMtd?.available ? formatMtdBrief(currentMtd) : mode.detail;
    container.append(note);
    if (this.currentValidation.reason) {
      const reason = document.createElement("p");
      reason.className = "panel-note warning-note full-row";
      reason.textContent = localizeErrorMessage(this.currentValidation.reason);
      container.append(reason);
    }
    const advanced = detailsBlock(zh.advanced.title);
    const advancedGrid = document.createElement("div");
    advancedGrid.className = "status-grid";
    advancedGrid.append(
      this.statusItem("可查表", this.currentValidation.canQuery ? "是" : "否", this.currentValidation.canQuery ? "drawing" : "warning"),
      this.statusItem(zh.advanced.denseIndex, this.currentValidation.denseIndex?.toString() ?? "-"),
      this.statusItem("表库层", this.currentValidation.layerName ?? "-"),
      this.statusItem("合法着法", String(generateLegalMoves(this.position).length)),
      this.statusItem("最佳着法", String(this.currentSummary?.bestMoves.length ?? 0)),
      this.statusItem("推荐详情", mode.detail),
    );
    if (currentMtd?.available) {
      advancedGrid.append(
        this.statusItem("materialTarget", String(currentMtd.materialTarget ?? "-")),
        this.statusItem("guaranteeDistance", String(currentMtd.guaranteeDistance ?? "-")),
        this.statusItem("cannonMaxCaptures", String(currentMtd.cannonMaxCaptures ?? "-")),
        this.statusItem("soldierSaved", String(currentMtd.soldierSaved ?? "-")),
      );
    }
    advanced.append(advancedGrid);
    container.append(advanced);
    return container;
  }

  private currentMtdStatusText(): string {
    if (this.tablebase?.kind === "browser-files") return "浏览器文件模式 WDL-only";
    if (this.tablebaseStatus?.mtd?.loaded) return zh.mtd.currentMissing;
    return zh.mtd.notLoaded;
  }

  private renderBestMovePanel(): HTMLElement {
    if (!this.currentValidation.canQuery) {
      return this.renderEmptyState("当前局面暂不可查表", this.currentValidation.reason ?? "无法给出下一步建议。");
    }
    if (!this.currentSummary) {
      return this.renderEmptyState("尚未查询", this.tablebase ? zh.recommendation.waiting : "请先加载表库。");
    }
    const container = document.createElement("div");
    container.className = "best-card";
    const heading = document.createElement("h3");
    const bestCount = this.currentSummary.bestMoves.length;
    const mode = this.currentRecommendationMode();
    heading.textContent = bestCount === 0
      ? `${sideText(this.position.side)}无合法着法`
      : mode.isMtdExact ? zh.recommendation.mtdBestMove : zh.recommendation.wdlBestMove;
    container.append(heading);
    container.append(this.recommendationModeNote(mode));

    if (bestCount === 0) {
      const note = document.createElement("p");
      note.textContent = zh.recommendation.noLegalMove;
      container.append(note);
      return container;
    }

    const bestMove = this.bestMoveInfo();
    if (!bestMove) return this.renderEmptyState("尚未查询", zh.recommendation.waiting);

    const explanation = explainMove(this.position.side, this.position.soldiers.size, bestMove, mode);
    const headline = document.createElement("p");
    headline.className = "recommendation-headline";
    headline.textContent = explanation.headline;
    const detailText = document.createElement("p");
    detailText.className = "panel-note recommendation-detail";
    detailText.textContent = this.showMoveArrow
      ? explanation.detail
      : `${explanation.detail} 现在已隐藏箭头，可在棋盘工具栏重新显示。`;
    const notation = document.createElement("p");
    notation.className = "move-notation-muted";
    notation.textContent = explanation.notation;
    container.append(headline, detailText, notation);

    const detail = document.createElement("div");
    detail.className = "best-move-detail-grid compact-best-grid";
    detail.append(
      this.summaryStat("结果", outcomeBadge(bestMove.successorOutcome)),
      this.summaryStat("推荐模式", badge(mode.label, mode.tone)),
      this.summaryStat("排名", bestMove.rank !== undefined ? `#${bestMove.rank}` : "-"),
      this.summaryStat("兵数层", String(bestMove.successorSoldierCount)),
    );
    container.append(detail);

    if (explanation.mtdDetail) {
      const mtdSummary = document.createElement("p");
      mtdSummary.className = bestMove.successorMtd?.available ? "panel-note mtd-note" : "panel-note";
      mtdSummary.textContent = explanation.mtdDetail;
      container.append(mtdSummary);
    }
    container.append(renderMtdLadder(this.position.soldiers.size, bestMove.successorOutcome, bestMove.successorMtd));

    const reasonText = bestMove.reason ?? bestMoveExplanation(this.currentSummary, mode);
    if (reasonText) {
      const reason = document.createElement("p");
      reason.className = "panel-note";
      reason.textContent = reasonText;
      container.append(reason);
    }

    if (bestMove.successorMtd?.available && bestMove.successorMtd.text) {
      const mtd = detailsBlock("MTD 说明");
      const text = document.createElement("p");
      text.className = "panel-note";
      text.textContent = bestMove.successorMtd.text;
      mtd.append(text);
      container.append(mtd);
    }
    if ((this.tablebaseResult?.optimalMoveCount ?? bestCount) > 1) {
      const optimalNote = document.createElement("p");
      optimalNote.className = "panel-note";
      optimalNote.textContent = zh.recommendation.multipleOptimal(this.tablebaseResult?.optimalMoveCount ?? bestCount);
      container.append(optimalNote);
    }

    const actions = document.createElement("div");
    actions.className = "best-actions";
    actions.append(
      this.makeButton(zh.recommendation.executeMove, () => this.applyMoveFromPanel(bestMove.move)),
      this.makeButton(zh.recommendation.copyPosition, () => void this.copyText(positionToNotation(this.position), zh.feedback.copied)),
      this.makeButton(zh.recommendation.copyAfterPosition, () => void this.copyText(positionToNotation(applyMove(this.position, bestMove.move)), zh.feedback.copiedAfter)),
      this.makeButton(zh.recommendation.copyMove, () => void this.copyText(formatMove(bestMove.move), zh.feedback.copiedMove)),
    );
    container.append(actions);
    return container;
  }

  private recommendationModeNote(mode: RecommendationModeView): HTMLElement {
    const note = document.createElement("p");
    note.className = mode.isMtdExact ? "panel-note" : "panel-note warning-note";
    note.append(badge(mode.label, mode.tone), document.createTextNode(` ${mode.detail}`));
    return note;
  }

  private renderSideComparisonPanel(): HTMLElement {
    if (!this.sideComparison) {
      return this.renderEmptyState("尚未比较", this.currentValidation.reason ?? "加载表库后会自动比较炮方先走和兵方先走。");
    }
    const container = document.createElement("div");
    container.className = "comparison-grid";
    container.append(this.renderSideComparisonEntry(this.sideComparison.cannon));
    container.append(this.renderSideComparisonEntry(this.sideComparison.soldier));
    return container;
  }

  private renderSideComparisonEntry(entry: SideComparisonResult["cannon"]): HTMLElement {
    const card = document.createElement("section");
    card.className = `comparison-card ${entry.side}`;
    const title = document.createElement("h3");
    title.textContent = `${sideText(entry.side)}先走`;
    card.append(title);
    if (!entry.summary) {
      const reason = document.createElement("p");
      reason.textContent = localizeErrorMessage(entry.validation.reason ?? entry.error ?? "无法比较。");
      card.append(reason);
      return card;
    }
    card.append(
      this.summaryStat("结果", outcomeBadge(entry.summary.outcome)),
      this.summaryStat("最佳着法", entry.summary.bestMoves[0] ? formatMove(entry.summary.bestMoves[0].move) : "无合法着法"),
      this.summaryStat("最佳着法数", String(entry.summary.bestMoves.length)),
      this.summaryStat("坏着数", String(entry.summary.losingMoves.length)),
    );
    return card;
  }

  private renderMoveGroupsPanel(): HTMLElement {
    if (!this.currentSummary) {
      return this.renderEmptyState("尚未查询", this.currentValidation.reason ?? "加载表库后会列出全部合法着法。");
    }
    const container = document.createElement("div");
    container.className = "tablebase-result-grid";
    const summary = document.createElement("div");
    summary.className = "summary-band";
    const bestTier = this.currentSummary.bestMoves[0]?.score?.wdlTier;
    const optimalMoves = this.currentSummary.allMoves.filter((move) => move.isBest);
    const acceptableMoves = this.currentSummary.allMoves.filter((move) =>
      !move.isBest &&
      (bestTier !== undefined ? move.score?.wdlTier === bestTier : move.classification !== "losing"));
    const mistakeMoves = this.currentSummary.allMoves.filter((move) =>
      !move.isBest &&
      (bestTier !== undefined ? move.score?.wdlTier !== bestTier : move.classification === "losing"));
    summary.append(
      this.summaryStat(zh.moveGroups.total, String(this.currentSummary.legalMoveCount)),
      this.summaryStat(zh.moveGroups.optimal, String(optimalMoves.length)),
      this.summaryStat(zh.moveGroups.acceptable, String(acceptableMoves.length)),
      this.summaryStat(zh.moveGroups.mistake, String(mistakeMoves.length)),
      this.summaryStat(zh.moveGroups.policy, this.currentRecommendationMode().label),
    );
    container.append(summary);
    container.append(renderMoveFunnel(this.currentSummary, {
      optimal: optimalMoves,
      acceptable: acceptableMoves,
      mistake: mistakeMoves,
    }, this.currentRecommendationMode()));

    const groups: Array<{ key: "optimal" | "acceptable" | "mistake"; title: string; moves: MoveOutcomeInfo[] }> = [
      {
        key: "optimal",
        title: zh.moveGroups.optimal,
        moves: optimalMoves,
      },
      {
        key: "acceptable",
        title: zh.moveGroups.acceptable,
        moves: acceptableMoves,
      },
      {
        key: "mistake",
        title: zh.moveGroups.mistake,
        moves: mistakeMoves,
      },
    ];

    for (const group of groups) {
      const section = document.createElement("section");
      section.className = `move-group ${group.key}`;
      const title = document.createElement("h3");
      title.textContent = group.title;
      section.append(title);
      if (group.moves.length === 0) {
          const empty = document.createElement("p");
          empty.className = "empty-group";
          empty.textContent = zh.moveGroups.empty;
          section.append(empty);
      } else {
        const list = document.createElement("ul");
        for (const move of group.moves) {
          const item = document.createElement("li");
          item.append(this.renderMoveButton(move, move.isBest));
          list.append(item);
        }
        section.append(list);
      }
      container.append(section);
    }
    return container;
  }

  private renderMoveButton(move: MoveOutcomeInfo, isBest: boolean): HTMLElement {
    const card = document.createElement("article");
    card.className = `move-row-card ${isBest ? "recommended" : move.classification === "losing" ? "mistake" : ""}`;
    const mtd = move.successorMtd?.available
      ? formatMoveMtdBrief(move.successorMtd)
      : "MTD -";
    const rank = move.rank !== undefined ? `#${move.rank}` : "-";
    const top = document.createElement("div");
    top.className = "move-row-top";
    top.append(
      textSpan("move-main", `${rank} ${formatMove(move.move)}`),
      outcomeBadge(move.successorOutcome),
      classificationBadge(move.classification),
    );
    card.append(top);
    card.append(textSpan("move-detail", [
      `k=${move.successorSoldierCount}`,
      move.move.capture ? `吃 ${move.move.capturedSquare}` : "不吃子",
      mtd,
    ].join(" | ")));
    const reasonText = move.reason ?? move.score?.description;
    if (reasonText) {
      card.append(textSpan("move-reason", reasonText));
    }

    const advanced = detailsBlock(zh.moveGroups.advanced);
    const advancedGrid = document.createElement("div");
    advancedGrid.className = "status-grid";
    advancedGrid.append(
      this.statusItem(zh.advanced.successorIndex, move.successorIndex.toString()),
      this.statusItem("score", formatScoreBrief(move.score)),
      this.statusItem("score.description", move.score?.description ?? "-"),
      this.statusItem("successorMtd", move.successorMtd?.available ? JSON.stringify(move.successorMtd) : "-"),
    );
    advanced.append(advancedGrid);
    card.append(advanced);

    const actions = document.createElement("div");
    actions.className = "move-row-actions";
    const previewButton = this.makeButton(zh.moveGroups.preview, () => this.previewMove(move.move));
    const executeButton = this.makeButton(zh.moveGroups.execute, () => this.applyMoveFromPanel(move.move));
    previewButton.addEventListener("click", (event) => event.stopPropagation());
    executeButton.addEventListener("click", (event) => event.stopPropagation());
    actions.append(previewButton, executeButton);
    card.append(actions);

    card.addEventListener("mouseenter", () => {
      this.hoveredMove = move.move;
      this.render();
    });
    card.addEventListener("mouseleave", () => {
      this.hoveredMove = null;
      this.render();
    });
    card.addEventListener("click", () => this.previewMove(move.move));
    return card;
  }

  private previewMove(move: Move): void {
    this.selectedMoveForDisplay = move;
    this.hoveredMove = null;
    this.showFeedback(`已预览着法：${formatMove(move)}。`);
    this.render();
  }

  private applyBestMove(): void {
    const move = this.primaryRecommendedMove();
    if (!move) {
      this.showFeedback(zh.recommendation.noLegalMove, true);
      return;
    }
    this.applyMoveFromPanel(move);
  }

  private applyMoveFromPanel(move: Move): void {
    this.pushUndo();
    this.position = applyMove(this.position, move);
    this.lastMove = move;
    this.afterPositionChanged(`已走：${formatMove(move)}。`);
  }

  private async selectTablebase(): Promise<void> {
    this.tablebaseStatusEl.textContent = zh.tablebase.selectingDirectory;
    try {
      this.tablebase = new BrowserFileTablebaseProvider(await openTablebaseDirectory());
      this.tablebaseStatus = await this.tablebase.status();
      this.showFeedback(zh.tablebase.loaded);
      this.renderTablebaseStatus();
      await this.refreshAnalysisNow();
    } catch (error) {
      this.tablebase = null;
      this.tablebaseStatus = null;
      this.tablebaseStatusEl.textContent = localizeErrorMessage(error instanceof Error ? error.message : String(error));
      this.scheduleAnalysis();
    }
  }

  private async selectTablebaseFiles(): Promise<void> {
    this.tablebaseStatusEl.textContent = zh.tablebase.selectingFiles;
    try {
      this.tablebase = new BrowserFileTablebaseProvider(await openTablebaseFiles());
      this.tablebaseStatus = await this.tablebase.status();
      this.showFeedback(zh.tablebase.filesLoaded);
      this.renderTablebaseStatus();
      await this.refreshAnalysisNow();
    } catch (error) {
      this.tablebase = null;
      this.tablebaseStatus = null;
      this.tablebaseStatusEl.textContent = localizeErrorMessage(error instanceof Error ? error.message : String(error));
      this.scheduleAnalysis();
    }
  }

  private renderTablebaseStatus(): void {
    if (!this.tablebase) {
      if (this.tablebaseStatus?.mode === "local-backend") {
        const detail = this.tablebaseStatus.error ?? "本地后端已响应，但表库未完整加载。";
        this.tablebaseStatusEl.replaceChildren(this.renderEmptyState("本地后端不可用", detail));
        return;
      }
      this.tablebaseStatusEl.replaceChildren(this.renderEmptyState(
        zh.tablebase.notLoadedTitle,
        zh.tablebase.notLoadedDetail,
      ));
      return;
    }
    const status = this.tablebaseStatus;
    if (!status) {
      this.tablebaseStatusEl.replaceChildren(this.renderEmptyState("尚未读取状态", "请选择表库或连接本地后端。"));
      return;
    }
    const container = document.createElement("div");
    container.className = "diagnostic-stack";
    container.append(renderTablebaseCoverage(status));

    const source = document.createElement("section");
    source.className = "diagnostic-section";
    source.append(
      this.sectionHeading("后端/来源"),
      this.statusGrid(
        this.statusItem("模式", this.tablebase.kind === "backend" ? "本地后端" : "浏览器文件"),
        this.statusItem(zh.labels.source, this.tablebase.kind === "backend" ? status.sourceName : status.sourceName),
        this.statusItem(zh.labels.readMode, this.tablebase.kind === "backend" ? "后端随机读取 .s15res" : zh.tablebase.randomRead),
      ),
    );
    container.append(source);

    const wdl = document.createElement("section");
    wdl.className = "diagnostic-section";
    wdl.append(
      this.sectionHeading("WDL 表库"),
      this.statusGrid(
        this.statusItem(zh.labels.status, status.complete ? zh.tablebase.complete : zh.tablebase.partial, status.complete ? "drawing" : "warning"),
        this.statusItem(zh.labels.layers, `${status.loadedLayers.length}/16`),
        this.statusItem("缺失层", status.missingLayers.length ? status.missingLayers.join(",") : "无"),
        this.statusItem(zh.labels.encoding, status.encoding || zh.outcome.Unknown),
        this.statusItem(zh.labels.ruleset, status.rulesetHash ?? status.rulesetName ?? "-"),
      ),
    );
    if (status.invalidLayers?.length) {
      const invalid = document.createElement("p");
      invalid.className = "panel-note warning-note";
      invalid.textContent = `无效 WDL 层：${status.invalidLayers.map((layer) => `${layer.soldierCount}:${layer.error}`).join("；")}`;
      wdl.append(invalid);
    }
    container.append(wdl);

    const mtdSection = document.createElement("section");
    mtdSection.className = "diagnostic-section";
    if (status.mtd) {
      const mtdText = status.mtd.complete
        ? zh.mtd.loaded
        : status.mtd.loaded
          ? zh.mtd.partial
          : zh.mtd.notLoaded;
      mtdSection.append(
        this.sectionHeading("MTD 表库"),
        this.statusGrid(
        this.statusItem("MTD", mtdText, status.mtd.loaded ? (status.mtd.complete ? "drawing" : "warning") : "warning"),
        this.statusItem("MTD 层数", `${status.mtd.loadedLayers.length}/16`),
        this.statusItem("缺失层", status.mtd.missingLayers.length ? status.mtd.missingLayers.join(",") : "无"),
        this.statusItem("存储", status.mtd.store),
        this.statusItem("版本", String(status.mtd.semanticVersion)),
        this.statusItem("编码", status.mtd.encoding),
        ),
      );
      const mtdNote = document.createElement("p");
      mtdNote.className = `panel-note full-row${status.mtd.complete ? "" : " warning-note"}`;
      mtdNote.textContent = status.mtd.complete
        ? "本地后端已加载完整 MTD；推荐会在数据完整的层使用 MTD tie-break。"
        : status.mtd.loaded
          ? zh.mtd.backendMtdPartialNote
          : (status.mtd.error ?? zh.mtd.backendNoMtdNote);
      mtdSection.append(mtdNote);
      if (status.mtd.invalidLayers?.length) {
        const invalid = document.createElement("p");
        invalid.className = "panel-note warning-note";
        invalid.textContent = `无效 MTD 层：${status.mtd.invalidLayers.map((layer) => `${layer.soldierCount}:${layer.error}`).join("；")}`;
        mtdSection.append(invalid);
      }
    } else {
      mtdSection.append(
        this.sectionHeading("MTD 表库"),
        this.renderEmptyState("浏览器文件模式", zh.mtd.browserFilesTablebaseNote),
      );
    }
    container.append(mtdSection);
    this.tablebaseStatusEl.replaceChildren(container);
  }

  private sectionHeading(text: string): HTMLElement {
    const heading = document.createElement("h3");
    heading.textContent = text;
    return heading;
  }

  private statusGrid(...items: HTMLElement[]): HTMLElement {
    const grid = document.createElement("div");
    grid.className = "status-grid";
    grid.append(...items);
    return grid;
  }

  private async exploreLine(): Promise<void> {
    this.stopAutoplay();
    const maxPlies = Number(this.maxPliesInputEl.value);
    this.lineResultEl.textContent = zh.line.exploring;
    const result = this.tablebase
      ? await this.tablebase.explore(this.position, maxPlies)
      : {
          start: this.position,
          startOutcome: "Unknown" as const,
          plies: [],
          stopReason: "missingTablebase" as const,
          cycleStartPly: null,
          error: "Select a tablebase directory or layer files first.",
        };
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
      if (ply.ply === this.activeLinePly) item.classList.add("active");
      if (result.cycleStartPly === ply.ply) item.classList.add("cycle-start");
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
    if (stopPlayback) this.stopAutoplay();
    this.activeLinePly = plyIndex;
    this.position = clonePosition(ply.position);
    this.lastMove = plyIndex === 0 ? null : this.lineResult.plies[plyIndex - 1]?.chosenMove ?? null;
    this.spotlightMove = ply.chosenMove;
    this.clearSelection();
    this.refreshLineListHighlight();
    this.render();
    this.renderPlaybackStatus();
    this.scheduleAnalysis();
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
      if (!this.lineResult || this.activeLinePly + 1 >= this.lineResult.plies.length) {
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
    await this.copyText(notation, zh.feedback.copied);
  }

  private async copyText(text: string, successMessage: string): Promise<void> {
    try {
      await navigator.clipboard.writeText(text);
    } catch {
      // Keeping the input populated is a sufficient fallback for non-secure origins.
    }
    this.showFeedback(successMessage);
  }

  private pasteNotationFromInput(): void {
    try {
      const next = parseEditablePositionNotation(this.pasteInputEl.value);
      this.setPosition(next, null, true, zh.feedback.pasted);
    } catch (error) {
      this.showFeedback(localizeErrorMessage(error instanceof Error ? error.message : String(error)), true);
    }
  }

  private onSquareClick(square: number): void {
    if (this.mode === "edit") {
      this.onEditSquareClick(square);
      return;
    }
    const move = this.selectedMoves.find((candidate) => candidate.to === square);
    if (move) {
      this.applyMoveFromPanel(move);
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
    this.updateTargetOutcomes();
    this.render();
  }

  private onEditSquareClick(square: number): void {
    if (this.editTool !== "select") {
      this.pushUndo();
      this.position = applyEditTool(this.position, square, this.editTool);
      this.lastMove = null;
      this.afterPositionChanged("已编辑局面。");
      return;
    }

    const occupied = this.position.cannons.has(square) || this.position.soldiers.has(square);
    if (this.editSelectedSquare === null) {
      this.editSelectedSquare = occupied ? square : null;
      this.render();
      return;
    }
    if (this.editSelectedSquare === square) {
      this.editSelectedSquare = null;
      this.render();
      return;
    }
    this.pushUndo();
    this.position = movePieceFreely(this.position, this.editSelectedSquare, square);
    this.lastMove = null;
    this.afterPositionChanged("已移动棋子。");
  }

  private updateTargetOutcomes(): void {
    if (!this.tablebaseResult) {
      this.targetOutcomes = [];
      return;
    }
    this.targetOutcomes = this.selectedMoves
      .map((move) => this.tablebaseResult?.moves.find((item) => movesSame(item.move, move)))
      .filter((move): move is NonNullable<typeof move> => !!move)
      .map((move) => ({
        move: move.move,
        outcome: move.successorOutcome,
        classification: move.classification,
      }));
  }

  private clearSelection(): void {
    this.selectedSquare = null;
    this.selectedMoves = [];
    this.targetOutcomes = [];
    this.selectedMoveForDisplay = null;
  }

  private render(): void {
    this.updateTargetOutcomes();
    renderBoard(this.boardEl, {
      position: this.position,
      selectedSquare: this.selectedSquare,
      editSelectedSquare: this.editSelectedSquare,
      legalMoves: this.selectedMoves,
      targetOutcomes: this.targetOutcomes,
      lastMove: this.lastMove,
      lineMove: this.currentLineMove(),
      spotlightMove: this.spotlightMove,
      orientation: this.boardFlipped ? "soldier" : "cannon",
      primaryMove: this.primaryRecommendedMove(),
      selectedMove: this.selectedMoveForDisplay,
      hoveredMove: this.hoveredMove,
      showSquareNumbers: this.showSquareNumbers,
      showMoveArrow: this.showMoveArrow,
      onSquareClick: (square) => this.onSquareClick(square),
    });

    this.turnEl.replaceChildren(sideBadge(this.position.side));
    this.cannonCountEl.textContent = String(this.position.cannons.size);
    this.soldierCountEl.textContent = String(this.position.soldiers.size);
    const displayOutcome = this.currentSummary?.outcome ?? terminalOutcome(this.position);
    this.resultEl.replaceChildren(this.currentValidation.canQuery || displayOutcome !== "Unknown" ? outcomeBadge(displayOutcome) : badge("不可查", "warning"));
    this.notationEl.textContent = positionToNotation(this.position);
    this.renderHeaderStatus();
  }

  private renderHeaderStatus(): void {
    this.headerStatusEl.replaceChildren(
      badge(zh.rulesetBadge, "neutral"),
      badge(this.mode === "analysis" ? "分析模式" : "编辑模式", this.mode === "analysis" ? "drawing" : "warning"),
      this.currentSummary ? outcomeBadge(this.currentSummary.outcome) : badge(this.currentValidation.canQuery ? "待查询" : "不可查", this.currentValidation.canQuery ? "neutral" : "warning"),
      badge(this.tablebase ? zh.tablebase.loadedBadge(this.tablebaseStatus?.loadedLayers.length ?? 0) : zh.tablebase.notLoadedBadge, this.tablebase ? "drawing" : "warning"),
      badge(this.currentRecommendationMode().label, this.currentRecommendationMode().tone),
      badge(this.boardFlipped ? zh.board.soldierView : zh.board.cannonView, "neutral"),
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
    if (typeof value === "string") val.textContent = value;
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
      this.makeButton("应用初始局面", () => this.setPosition(initialPosition(), null, true, zh.initial.restored)),
      this.makeButton(zh.actions.showDrawingMove, () => {
        this.spotlightMove = drawingFirstMove;
        this.showFeedback(zh.initial.highlighted);
        this.render();
      }),
      this.makeButton("应用保和首着后局面", () => this.setPosition(parseEditablePositionNotation(initialAfterDrawingMoveNotation), drawingFirstMove, true, "已应用保和首着后局面。")),
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
    for (const item of [
      ...zh.help,
      "编辑模式可以任意放炮、放兵、移动或删除棋子；表库查询仍要求正好 3 个炮、0..15 个兵。",
      "棋盘目标格上的小标签显示该着法后继的胜负和结果。",
    ]) {
      const li = document.createElement("li");
      li.textContent = item;
      list.append(li);
    }
    this.helpEl.replaceChildren(list);
  }
}

function textSpan(className: string, text: string): HTMLSpanElement {
  const span = document.createElement("span");
  span.className = className;
  span.textContent = text;
  return span;
}

function movesSame(left: Move, right: Move): boolean {
  return (
    left.from === right.from &&
    left.to === right.to &&
    left.capture === right.capture &&
    left.capturedSquare === right.capturedSquare
  );
}

function bestMoveExplanation(summary: BestMoveSummary, mode?: RecommendationModeView): string {
  if (mode?.isMtdExact) {
    return "这是按 WDL 结果和 MTD 细分规则共同排序后的首选着法。";
  }
  if (summary.bestMoves.length === 0) {
    return "当前方无合法着法。";
  }
  const first = summary.bestMoves[0];
  if (summary.outcome === "Draw" && first.classification === "drawing") {
    return summary.bestMoves.length === 1
      ? "这是当前局面的唯一保和着法。"
      : `${sideText(summary.side)}有 ${summary.bestMoves.length} 个保持和棋的着法，任选其一都不改变胜负和结果。`;
  }
  if (first.classification === "winning") {
    return `${sideText(summary.side)}处于胜势，以下着法保持胜势。`;
  }
  if (first.classification === "losing") {
    return "当前方处于理论败势，以下是表库下的最佳尝试，但不能改变最终结果。";
  }
  return "以下着法保持当前胜负和结果。";
}
