import type { Move, Position } from "./engine";
import { boardSize, movesEqual } from "./engine";

export interface BoardRenderOptions {
  position: Position;
  selectedSquare: number | null;
  legalMoves: Move[];
  recommendedMoves?: Move[];
  lastMove?: Move | null;
  onSquareClick: (square: number) => void;
}

function pieceLabel(position: Position, square: number): string {
  if (position.soldiers.has(square)) return "Soldier";
  if (position.cannons.has(square)) return "Cannon";
  return "Empty";
}

export function renderBoard(container: HTMLElement, options: BoardRenderOptions): void {
  container.innerHTML = "";
  const targetSquares = new Map(options.legalMoves.map((move) => [move.to, move]));
  const recommended = options.recommendedMoves ?? [];

  for (let square = 0; square < boardSize * boardSize; square += 1) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "square";
    button.dataset.square = String(square);
    button.setAttribute("aria-label", `${pieceLabel(options.position, square)} on square ${square}`);

    if (options.position.soldiers.has(square)) {
      button.textContent = "S";
      button.classList.add("soldier");
    } else if (options.position.cannons.has(square)) {
      button.textContent = "C";
      button.classList.add("cannon");
    } else {
      button.textContent = "";
      button.classList.add("empty");
    }

    if (options.selectedSquare === square) {
      button.classList.add("selected");
    }

    const targetMove = targetSquares.get(square);
    if (targetMove) {
      button.classList.add(targetMove.capture ? "capture-target" : "move-target");
    }

    if (options.lastMove && (options.lastMove.from === square || options.lastMove.to === square)) {
      button.classList.add("last-move");
    }

    if (recommended.some((move) => move.from === square || move.to === square)) {
      button.classList.add("recommended-move");
    }

    if (targetMove && recommended.some((move) => movesEqual(move, targetMove))) {
      button.classList.add("recommended-target");
    }

    button.addEventListener("click", () => options.onSquareClick(square));
    container.append(button);
  }
}
