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
  recommendedMoves?: Move[];
  targetOutcomes?: TargetOutcomeLabel[];
  lastMove?: Move | null;
  lineMove?: Move | null;
  spotlightMove?: Move | null;
  orientation?: "cannon" | "soldier";
  primaryMove?: Move | null;
  selectedMove?: Move | null;
  hoveredMove?: Move | null;
  showSquareNumbers?: boolean;
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

function ariaLabelForSquare(options: BoardRenderOptions, square: number, targetMove: Move | undefined, displayMove: Move | null): string {
  const parts = [`第 ${square} 格`, pieceLabel(options.position, square)];
  if (options.selectedSquare === square || options.editSelectedSquare === square) parts.push("已选中");
  if (targetMove) parts.push(targetMove.capture ? "合法吃子目标" : "合法移动目标");
  if (displayMove?.from === square) parts.push("推荐着法起点");
  if (displayMove?.to === square) parts.push("推荐着法目标");
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
      button.classList.add("selected");
    }

    const targetMove = targetSquares.get(square);
    if (targetMove) {
      button.classList.add(targetMove.capture ? "capture-target" : "move-target");
      const labelInfo = targetOutcomes.get(square);
      if (labelInfo) {
        const label = document.createElement("span");
        label.className = `target-outcome ${labelInfo.classification}`;
        label.textContent = `${labelInfo.move.capture ? "吃 " : ""}${outcomeText(labelInfo.outcome)}`;
        label.title = `${formatMove(labelInfo.move)}，后继：${outcomeText(labelInfo.outcome)}，${classificationText(labelInfo.classification)}`;
        button.append(label);
      }
    }

    if (displayKind === "primary" && displayMove?.from === square) {
      button.classList.add("primary-from");
    }

    if (displayKind === "primary" && displayMove?.to === square) {
      button.classList.add("primary-to");
      if (displayMove.capture) button.classList.add("primary-capture");
    }

    if (displayKind === "hovered" && displayMove?.from === square) {
      button.classList.add("hovered-move-from");
    }

    if (displayKind === "hovered" && displayMove?.to === square) {
      button.classList.add("hovered-move-to");
    }

    if (displayKind === "selected" && displayMove?.from === square) {
      button.classList.add("selected-move-from");
    }

    if (displayKind === "selected" && displayMove?.to === square) {
      button.classList.add("selected-move-to");
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
      button.classList.add("recommended-target");
    }

    button.setAttribute("aria-label", ariaLabelForSquare(options, square, targetMove, displayMove));
    button.addEventListener("click", () => options.onSquareClick(square));
    container.append(button);
  }
}
