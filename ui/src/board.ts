import type { Move, Position } from "./engine";
import { boardSize, movesEqual } from "./engine";
import { classificationText, formatMove, outcomeText, zh } from "./i18n/zh";
import type { Outcome } from "./engine";
import type { MoveClassification } from "./tablebase/recommend";
import { displayIndexToSquare, type BoardOrientation } from "./boardOrientation";
export { displayIndexToSquare, squareToDisplayIndex, type BoardOrientation } from "./boardOrientation";

const cannonPieceUrl = new URL("./assets/pieces/cannon.svg", import.meta.url).href;
const soldierPieceUrl = new URL("./assets/pieces/soldier.svg", import.meta.url).href;

export interface BoardRenderOptions {
  position: Position;
  orientation: BoardOrientation;
  selectedSquare: number | null;
  legalMoves: Move[];
  editSelectedSquare?: number | null;
  recommendedMoves?: Move[];
  targetOutcomes?: TargetOutcomeLabel[];
  lastMove?: Move | null;
  lineMove?: Move | null;
  spotlightMove?: Move | null;
  showSquareLabels?: boolean;
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

export function renderBoard(container: HTMLElement, options: BoardRenderOptions): void {
  container.innerHTML = "";
  container.dataset.orientation = options.orientation;
  const targetSquares = new Map(options.legalMoves.map((move) => [move.to, move]));
  const targetOutcomes = new Map((options.targetOutcomes ?? []).map((target) => [target.move.to, target]));
  const recommended = options.recommendedMoves ?? [];

  for (let displayIndex = 0; displayIndex < boardSize * boardSize; displayIndex += 1) {
    const square = displayIndexToSquare(displayIndex, options.orientation);
    const button = document.createElement("button");
    button.type = "button";
    button.className = "square";
    button.dataset.square = String(square);
    button.dataset.displayIndex = String(displayIndex);
    button.setAttribute("aria-label", `${pieceLabel(options.position, square)}，第 ${square} 格`);

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

    if (moveTouchesSquare(options.lastMove, square)) {
      button.classList.add("last-move");
    }

    if (recommended.some((move) => move.from === square)) {
      button.classList.add("recommended-from");
    }

    if (recommended.some((move) => move.to === square)) {
      button.classList.add("recommended-move");
    }

    if (targetMove && recommended.some((move) => movesEqual(move, targetMove))) {
      button.classList.add("recommended-target");
    }

    if (moveTouchesSquare(options.lineMove, square)) {
      button.classList.add("line-move");
    }

    if (moveTouchesSquare(options.spotlightMove, square)) {
      button.classList.add("spotlight-move");
    }

    if (options.showSquareLabels) {
      const index = document.createElement("span");
      index.className = "square-index";
      index.textContent = String(square);
      button.append(index);
    }

    button.addEventListener("click", () => options.onSquareClick(square));
    container.append(button);
  }
}
