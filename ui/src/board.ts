import type { Move, Position } from "./engine";
import { boardSize } from "./engine";

export interface BoardRenderOptions {
  position: Position;
  selectedSquare: number | null;
  legalMoves: Move[];
  onSquareClick: (square: number) => void;
}

export function renderBoard(container: HTMLElement, options: BoardRenderOptions): void {
  container.innerHTML = "";
  const targetSquares = new Map(options.legalMoves.map((move) => [move.to, move]));

  for (let square = 0; square < boardSize * boardSize; square += 1) {
    const button = document.createElement("button");
    button.type = "button";
    button.className = "square";
    button.dataset.square = String(square);
    button.setAttribute("aria-label", `格 ${square}`);

    if (options.position.soldiers.has(square)) {
      button.textContent = "兵";
      button.classList.add("soldier");
    } else if (options.position.cannons.has(square)) {
      button.textContent = "炮";
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

    button.addEventListener("click", () => options.onSquareClick(square));
    container.append(button);
  }
}
