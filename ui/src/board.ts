import type { Move, Position } from "./engine";
import { boardSize, movesEqual } from "./engine";
import { classificationText, formatMove, outcomeText, zh } from "./i18n/zh";
import type { Outcome } from "./engine";
import type { MoveClassification } from "./tablebase/recommend";

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
  const token = document.createElement("span");
  token.className = `piece-token ${kind}-token`;
  const glyph = document.createElement("span");
  glyph.className = "piece-glyph";
  glyph.textContent = kind === "cannon" ? zh.piece.cannon : zh.piece.soldier;
  token.append(glyph);
  button.append(token);
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
  return {
    x: 10 + column * 20,
    y: 10 + row * 20,
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

function appendBoardLines(container: HTMLElement): void {
  const namespace = "http://www.w3.org/2000/svg";
  const svg = document.createElementNS(namespace, "svg");
  svg.classList.add("board-lines");
  svg.setAttribute("viewBox", "0 0 100 100");
  svg.setAttribute("aria-hidden", "true");
  for (let index = 0; index < boardSize; index += 1) {
    const offset = 10 + index * 20;
    const vertical = document.createElementNS(namespace, "line");
    vertical.setAttribute("x1", String(offset));
    vertical.setAttribute("y1", "10");
    vertical.setAttribute("x2", String(offset));
    vertical.setAttribute("y2", "90");
    svg.append(vertical);

    const horizontal = document.createElementNS(namespace, "line");
    horizontal.setAttribute("x1", "10");
    horizontal.setAttribute("y1", String(offset));
    horizontal.setAttribute("x2", "90");
    horizontal.setAttribute("y2", String(offset));
    svg.append(horizontal);
  }
  container.append(svg);
}

function appendMoveArrow(container: HTMLElement, move: Move, visualSquares: number[], displayKind: string | null): void {
  const namespace = "http://www.w3.org/2000/svg";
  const from = visualCenter(visualSquares, move.from);
  const to = visualCenter(visualSquares, move.to);
  const line = shortenLine(from, to, 7.4);
  const bend = Math.abs(from.x - to.x) + Math.abs(from.y - to.y) > 21 ? -5 : -3;
  const controlX = (line.x1 + line.x2) / 2 + (line.y2 - line.y1 === 0 ? 0 : bend);
  const controlY = (line.y1 + line.y2) / 2 + (line.x2 - line.x1 === 0 ? 0 : bend);
  const svg = document.createElementNS(namespace, "svg");
  svg.classList.add("move-hint-layer");
  if (displayKind) svg.classList.add(displayKind);
  svg.setAttribute("viewBox", "0 0 100 100");
  svg.setAttribute("aria-hidden", "true");

  const defs = document.createElementNS(namespace, "defs");
  defs.innerHTML = `
    <filter id="move-hint-shadow" x="-20%" y="-20%" width="140%" height="140%">
      <feDropShadow dx="0" dy="1.2" stdDeviation="1.2" flood-color="rgb(31 40 36)" flood-opacity="0.28" />
    </filter>
    <marker id="move-hint-head" viewBox="0 0 10 10" refX="8.5" refY="5" markerWidth="4.2" markerHeight="4.2" orient="auto-start-reverse">
      <path d="M 0 0 L 10 5 L 0 10 z" />
    </marker>
  `;
  svg.append(defs);

  const path = document.createElementNS(namespace, "path");
  path.classList.add("move-hint-path");
  path.setAttribute("d", `M ${line.x1} ${line.y1} Q ${controlX} ${controlY} ${line.x2} ${line.y2}`);
  path.setAttribute("marker-end", "url(#move-hint-head)");
  svg.append(path);

  const dot = document.createElementNS(namespace, "circle");
  dot.classList.add("move-hint-origin");
  dot.setAttribute("cx", String(from.x));
  dot.setAttribute("cy", String(from.y));
  dot.setAttribute("r", "2.5");
  svg.append(dot);

  if (move.capture) {
    const ring = document.createElementNS(namespace, "circle");
    ring.classList.add("move-hint-capture");
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
  appendBoardLines(container);

  for (const square of visualSquares) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "square point-square";
    button.dataset.square = String(square);
    const center = visualCenter(visualSquares, square);
    button.style.left = `${center.x}%`;
    button.style.top = `${center.y}%`;

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
      button.classList.add("display-from", "hint-from");
      if (displayKind) button.dataset.displayKind = displayKind;
    }

    if (displayMove?.to === square) {
      button.classList.add("display-to", "hint-to");
      if (displayMove.capture) button.classList.add("display-capture", "hint-capture");
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
