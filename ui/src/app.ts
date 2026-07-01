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
  private resultTable: ResultTable | null = null;
  private tableLoadError: string | null = null;

  constructor(root: HTMLElement) {
    this.root = root;
  }

  mount(): void {
    this.root.className = "app-shell";
    this.root.innerHTML = `
      <header class="topbar">
        <div>
          <h1>Sanpao15 三炮十五兵</h1>
        </div>
      </header>
      <div class="game-layout">
        <div class="board-wrap"></div>
        <aside class="status-panel">
          <div class="metric"><span>当前回合</span></div>
          <div class="metric"><span>剩余兵数</span></div>
          <div class="metric"><span>结果</span></div>
        </aside>
      </div>
      <nav class="toolbar" aria-label="棋局操作"></nav>
      <section class="notation-panel">
        <span>当前局面字符串</span>
      </section>
      <section class="analysis-panel"></section>
    `;

    const boardWrap = this.root.querySelector(".board-wrap");
    const statusPanel = this.root.querySelector(".status-panel");
    const toolbar = this.root.querySelector(".toolbar");
    const notationPanel = this.root.querySelector(".notation-panel");
    const analysisPanel = this.root.querySelector(".analysis-panel");

    if (!boardWrap || !statusPanel || !toolbar || !notationPanel || !analysisPanel) {
      throw new Error("UI mount failed");
    }

    this.boardEl.className = "board";
    this.boardEl.setAttribute("aria-label", "5x5 棋盘");
    boardWrap.append(this.boardEl);

    const metrics = Array.from(statusPanel.querySelectorAll(".metric"));
    metrics[0].append(this.turnEl);
    metrics[1].append(this.soldierCountEl);
    metrics[2].append(this.resultEl);

    toolbar.append(
      this.makeButton("重新开始", () => this.reset()),
      this.makeButton("悔棋", () => this.undo()),
      this.makeButton("分析当前局面", () => this.analyze()),
    );

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
    this.render();
  }

  private async analyze(): Promise<void> {
    const outcome = terminalOutcome(this.position);
    const legalMoves = generateLegalMoves(this.position);
    this.analysisEl.textContent = "正在加载结果表...";

    if (!this.resultTable && !this.tableLoadError) {
      try {
        this.resultTable = await loadResultTable();
      } catch (error) {
        this.tableLoadError = error instanceof Error ? error.message : String(error);
      }
    }

    if (!this.resultTable) {
      const fallback = outcome !== "Unknown" ? `\n基础终局：${outcomeLabel(outcome)}` : "";
      this.analysisEl.textContent = `${this.tableLoadError ?? "结果表未加载。"}${fallback}\n当前共有 ${legalMoves.length} 个合法走法。`;
      return;
    }

    const analysis = analyzePositionWithTable(this.position, this.resultTable);
    const bestMove = analysis.bestMove ? `${analysis.bestMove.from} -> ${analysis.bestMove.to}` : "无";
    const tableType = this.resultTable.exact ? "完整" : this.resultTable.truncated ? "截断" : "部分";
    const foundText = analysis.foundInTable ? "已命中" : "未查到";
    const moveLines = analysis.legalMoves.map((item) => {
      const capture = item.move.capture ? ` 吃 ${item.move.capturedSquare}` : "";
      const best = item.isBest ? "，推荐" : "";
      const distance = item.distance >= 0 ? String(item.distance) : "-";
      return `${item.move.from} -> ${item.move.to}${capture}：${outcomeLabel(item.outcome)}，distance ${distance}${best}`;
    });

    this.analysisEl.textContent = [
      "结果表：已加载",
      `表类型：${tableType}`,
      `当前局面：${outcomeLabel(analysis.outcome)}（${foundText}）`,
      `距离：${analysis.distance >= 0 ? analysis.distance : "-"}`,
      `推荐着法：${bestMove}`,
      "",
      "合法着法：",
      ...(moveLines.length > 0 ? moveLines : ["无"]),
    ].join("\n");
  }

  private onSquareClick(square: number): void {
    const move = this.selectedMoves.find((candidate) => candidate.to === square);
    if (move) {
      this.history.push({ position: this.position });
      this.position = applyMove(this.position, move);
      this.clearSelection();
      this.analysisEl.textContent = "";
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
