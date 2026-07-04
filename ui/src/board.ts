import type { Move, Position } from "./engine";
import { boardSize, movesEqual } from "./engine";
import { classificationText, formatMove, outcomeText, zh } from "./i18n/zh";
import type { Outcome } from "./engine";
import type { MoveClassification } from "./tablebase/recommend";

const cannonPieceUrl = new URL("./assets/pieces/cannon.svg", import.meta.url).href;
const soldierPieceUrl = new URL("./assets/pieces/soldier.svg", import.meta.url).href;

export interface BoardRenderOptions {
  position: Position;
  selectedSquare: number | null;
  legalMoves: Move[];
  editSelectedSquare?: number | null;
  targetOutcomes?: TargetOutcomeLabel[];
  lastMove?: Move | null;
  lineMove?: Move | null;
  spotlightMove?: Move | null;
  orientation?: "cannon" | "soldier";
  primaryMove?: Move | null;
  selectedMove?: Move | null;
  hoveredMove?: Move | null;
  showSquareNumbers?: boolean;
  showMoveArrow?: boolean;
  onSquareClick: (square: number) => void;
}

export interface TargetOutcomeLabel {
  move: Move;
  outcome: Outcome;
  classification: MoveClassification;
}

function pieceLabel(position: Position, square: number): string {
  if (position.soldiers.has(square)) return zh.piece.soldier;
  if (position.cannons.has(square)) return zh.piece.cannon;
  return zh.piece.empty;
}

function appendPiece(button: HTMLButtonElement, kind: "cannon" | "soldier"): void {
  const image = document.createElement("img");
  image.className = "piece-image";
  image.alt = kind === "cannon" ? zh.piece.cannon : zh.piece.soldier;
  image.src = kind === "cannon" ? cannonPieceUrl : soldierPieceUrl;
  button.append(image);
}

function moveTouchesSquare(move: Move | null | undefined, square: number): boolean {
  return !!move && (move.from === square || move.to === square);
}

function visualSquaresForOrientation(orientation: "cannon" | "soldier"): number[] {
  const squares = Array.from({ length: boardSize * boardSize }, (_, index) => index);
  return orientation === "soldier" ? squares.reverse() : squares;
}

function appendSquareNumber(button: HTMLButtonElement, square: number): void {
  const squareNumber = document.createElement("span");
  squareNumber.className = "square-number";
  squareNumber.textContent = String(square);
  button.append(squareNumber);
}

function visualCenter(visualSquares: number[], square: number): { x: number; y: number } {
  const index = visualSquares.indexOf(square);
  const row = Math.floor(index / boardSize);
  const column = index % boardSize;
  const cell = 100 / boardSize;
  return {
    x: column * cell + cell / 2,
    y: row * cell + cell / 2,
  };
}

function shortenLine(
  from: { x: number; y: number },
  to: { x: number; y: number },
  inset: number,
): { x1: number; y1: number; x2: number; y2: number } {
  const dx = to.x - from.x;
  const dy = to.y - from.y;
  const length = Math.hypot(dx, dy);
  if (length === 0) return { x1: from.x, y1: from.y, x2: to.x, y2: to.y };
  const ux = dx / length;
  const uy = dy / length;
  return {
    x1: from.x + ux * inset,
    y1: from.y + uy * inset,
    x2: to.x - ux * inset,
    y2: to.y - uy * inset,
  };
}

function appendMoveArrow(container: HTMLElement, move: Move, visualSquares: number[], displayKind: string | null): void {
  const namespace = "http://www.w3.org/2000/svg";
  const from = visualCenter(visualSquares, move.from);
  const to = visualCenter(visualSquares, move.to);
  const line = shortenLine(from, to, 4.8);
  const svg = document.createElementNS(namespace, "svg");
  svg.classList.add("board-arrow-overlay");
  if (displayKind) svg.classList.add(displayKind);
  svg.setAttribute("viewBox", "0 0 100 100");
  svg.setAttribute("aria-hidden", "true");

  const defs = document.createElementNS(namespace, "defs");
  defs.innerHTML = `
    <filter id="board-arrow-shadow" x="-20%" y="-20%" width="140%" height="140%">
      <feDropShadow dx="0" dy="1.2" stdDeviation="1.2" flood-color="rgb(31 40 36)" flood-opacity="0.28" />
    </filter>
    <marker id="board-arrow-head" viewBox="0 0 10 10" refX="8.5" refY="5" markerWidth="5.2" markerHeight="5.2" orient="auto-start-reverse">
      <path d="M 0 0 L 10 5 L 0 10 z" />
    </marker>
  `;
  svg.append(defs);

  const path = document.createElementNS(namespace, "line");
  path.classList.add("board-arrow-line");
  path.setAttribute("x1", String(line.x1));
  path.setAttribute("y1", String(line.y1));
  path.setAttribute("x2", String(line.x2));
  path.setAttribute("y2", String(line.y2));
  path.setAttribute("marker-end", "url(#board-arrow-head)");
  svg.append(path);

  const dot = document.createElementNS(namespace, "circle");
  dot.classList.add("board-arrow-origin");
  dot.setAttribute("cx", String(from.x));
  dot.setAttribute("cy", String(from.y));
  dot.setAttribute("r", "2.5");
  svg.append(dot);

  if (move.capture) {
    const ring = document.createElementNS(namespace, "circle");
    ring.classList.add("board-arrow-capture");
    ring.setAttribute("cx", String(to.x));
    ring.setAttribute("cy", String(to.y));
    ring.setAttribute("r", "6.4");
    svg.append(ring);
  }

  container.append(svg);
}

function ariaLabelForSquare(options: BoardRenderOptions, square: number, targetMove: Move | undefined, displayMove: Move | null): string {
  const parts = [`第 ${square} 格`, pieceLabel(options.position, square)];
  if (options.selectedSquare === square || options.editSelectedSquare === square) parts.push("已选中");
  if (targetMove) parts.push(targetMove.capture ? "合法吃子目标" : "合法移动目标");
  if (displayMove?.from === square) parts.push("主显示着法起点");
  if (displayMove?.to === square) parts.push("主显示着法目标");
  if (moveTouchesSquare(options.lastMove, square)) parts.push("上一着经过");
  return parts.join("，");
}

export function renderBoard(container: HTMLElement, options: BoardRenderOptions): void {
  container.innerHTML = "";
  const targetSquares = new Map(options.legalMoves.map((move) => [move.to, move]));
  const targetOutcomes = new Map((options.targetOutcomes ?? []).map((target) => [target.move.to, target]));
  const orientation = options.orientation ?? "cannon";
  const visualSquares = visualSquaresForOrientation(orientation);
  const displayMove = options.selectedMove ?? options.hoveredMove ?? options.primaryMove ?? null;
  const displayKind = options.selectedMove ? "selected" : options.hoveredMove ? "hovered" : displayMove ? "primary" : null;

  for (const square of visualSquares) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "square";
    button.dataset.square = String(square);

    if (options.position.soldiers.has(square)) {
      button.classList.add("soldier");
      appendPiece(button, "soldier");
    } else if (options.position.cannons.has(square)) {
      button.classList.add("cannon");
      appendPiece(button, "cannon");
    } else {
      button.textContent = "";
      button.classList.add("empty");
    }

    if (options.showSquareNumbers ?? true) {
      appendSquareNumber(button, square);
    }

    if (options.selectedSquare === square || options.editSelectedSquare === square) {
      button.classList.add("selected-square");
    }

    const targetMove = targetSquares.get(square);
    if (targetMove) {
      button.classList.add(targetMove.capture ? "capture-target" : "legal-target");
      const labelInfo = targetOutcomes.get(square);
      if (labelInfo) {
        const label = document.createElement("span");
        label.className = `target-outcome ${labelInfo.classification}`;
        label.textContent = `${labelInfo.move.capture ? "吃 " : ""}${outcomeText(labelInfo.outcome)}`;
        label.title = `${formatMove(labelInfo.move)}，后继：${outcomeText(labelInfo.outcome)}，${classificationText(labelInfo.classification)}`;
        button.append(label);
      }
    }

    if (displayMove?.from === square) {
      button.classList.add("display-from");
      if (displayKind) button.dataset.displayKind = displayKind;
    }

    if (displayMove?.to === square) {
      button.classList.add("display-to");
      if (displayMove.capture) button.classList.add("display-capture");
      if (displayKind) button.dataset.displayKind = displayKind;
    }

    if (options.lastMove?.from === square) {
      button.classList.add("last-move-from");
    }

    if (options.lastMove?.to === square) {
      button.classList.add("last-move-to");
    }

    if (options.lineMove?.from === square) {
      button.classList.add("line-move-from");
    }

    if (options.lineMove?.to === square) {
      button.classList.add("line-move-to");
    }

    if (options.spotlightMove?.from === square) {
      button.classList.add("spotlight-from");
    }

    if (options.spotlightMove?.to === square) {
      button.classList.add("spotlight-to");
    }

    if (targetMove && displayMove && movesEqual(displayMove, targetMove)) {
      button.classList.add("display-target");
    }

    button.setAttribute("aria-label", ariaLabelForSquare(options, square, targetMove, displayMove));
    button.addEventListener("click", () => options.onSquareClick(square));
    container.append(button);
  }

  if ((options.showMoveArrow ?? true) && displayMove) {
    appendMoveArrow(container, displayMove, visualSquares, displayKind);
  }
}
