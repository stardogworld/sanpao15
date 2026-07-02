import type { Move, Position } from "./engine";
import { boardSize, movesEqual } from "./engine";

const cannonPieceUrl = new URL("./assets/pieces/cannon.svg", import.meta.url).href;
const soldierPieceUrl = new URL("./assets/pieces/soldier.svg", import.meta.url).href;

export interface BoardRenderOptions {
  position: Position;
  selectedSquare: number | null;
  legalMoves: Move[];
  recommendedMoves?: Move[];
  lastMove?: Move | null;
  lineMove?: Move | null;
  spotlightMove?: Move | null;
  onSquareClick: (square: number) => void;
}

function pieceLabel(position: Position, square: number): string {
  if (position.soldiers.has(square)) return "Soldier";
  if (position.cannons.has(square)) return "Cannon";
  return "Empty";
}

function appendPiece(button: HTMLButtonElement, kind: "cannon" | "soldier"): void {
  const image = document.createElement("img");
  image.className = "piece-image";
  image.alt = kind === "cannon" ? "Cannon" : "Soldier";
  image.src = kind === "cannon" ? cannonPieceUrl : soldierPieceUrl;

  const label = document.createElement("span");
  label.className = "piece-label";
  label.textContent = kind === "cannon" ? "C" : "S";

  button.append(image, label);
}

function moveTouchesSquare(move: Move | null | undefined, square: number): boolean {
  return !!move && (move.from === square || move.to === square);
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
      button.classList.add("soldier");
      appendPiece(button, "soldier");
    } else if (options.position.cannons.has(square)) {
      button.classList.add("cannon");
      appendPiece(button, "cannon");
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

    if (moveTouchesSquare(options.lastMove, square)) {
      button.classList.add("last-move");
    }

    if (recommended.some((move) => move.from === square || move.to === square)) {
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

    button.addEventListener("click", () => options.onSquareClick(square));
    container.append(button);
  }
}
