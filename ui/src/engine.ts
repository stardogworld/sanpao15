export type Side = "cannon" | "soldier";
export type Outcome = "Unknown" | "CannonWin" | "SoldierWin" | "Draw";

export interface Move {
  from: number;
  to: number;
  capture: boolean;
  capturedSquare: number;
}

export interface Position {
  cannons: Set<number>;
  soldiers: Set<number>;
  side: Side;
}

export const boardSize = 5;
export const squareCount = 25;

export function initialPosition(): Position {
  const soldiers = new Set<number>();
  for (let square = 0; square <= 14; square += 1) {
    soldiers.add(square);
  }
  return {
    cannons: new Set([21, 22, 23]),
    soldiers,
    side: "cannon",
  };
}

export function clonePosition(pos: Position): Position {
  return {
    cannons: new Set(pos.cannons),
    soldiers: new Set(pos.soldiers),
    side: pos.side,
  };
}

export function opposite(side: Side): Side {
  return side === "cannon" ? "soldier" : "cannon";
}

export function sideLabel(side: Side): string {
  return side === "cannon" ? "炮" : "兵";
}

export function outcomeLabel(outcome: Outcome): string {
  switch (outcome) {
    case "CannonWin":
      return "炮胜";
    case "SoldierWin":
      return "兵胜";
    case "Draw":
      return "和棋";
    case "Unknown":
      return "进行中";
  }
}

export function isValidSquare(square: number): boolean {
  return Number.isInteger(square) && square >= 0 && square < squareCount;
}

function rowOf(square: number): number {
  return Math.floor(square / boardSize);
}

function colOf(square: number): number {
  return square % boardSize;
}

export function orthogonalNeighbors(square: number): number[] {
  const row = rowOf(square);
  const col = colOf(square);
  const result: number[] = [];
  if (row > 0) result.push(square - boardSize);
  if (row + 1 < boardSize) result.push(square + boardSize);
  if (col > 0) result.push(square - 1);
  if (col + 1 < boardSize) result.push(square + 1);
  return result;
}

function cannonJumps(square: number): Array<{ over: number; landing: number }> {
  const row = rowOf(square);
  const col = colOf(square);
  const result: Array<{ over: number; landing: number }> = [];

  for (const [dr, dc] of [
    [-1, 0],
    [1, 0],
    [0, -1],
    [0, 1],
  ] as const) {
    const overRow = row + dr;
    const overCol = col + dc;
    const landingRow = row + 2 * dr;
    const landingCol = col + 2 * dc;
    if (
      overRow >= 0 &&
      overRow < boardSize &&
      overCol >= 0 &&
      overCol < boardSize &&
      landingRow >= 0 &&
      landingRow < boardSize &&
      landingCol >= 0 &&
      landingCol < boardSize
    ) {
      result.push({
        over: overRow * boardSize + overCol,
        landing: landingRow * boardSize + landingCol,
      });
    }
  }

  return result;
}

export function occupiedSquares(pos: Position): Set<number> {
  return new Set([...pos.cannons, ...pos.soldiers]);
}

export function generateCannonMoves(pos: Position): Move[] {
  const moves: Move[] = [];
  const occupied = occupiedSquares(pos);

  for (const from of pos.cannons) {
    for (const to of orthogonalNeighbors(from)) {
      if (!occupied.has(to)) {
        moves.push({ from, to, capture: false, capturedSquare: -1 });
      }
    }

    for (const jump of cannonJumps(from)) {
      if (!occupied.has(jump.over) && pos.soldiers.has(jump.landing)) {
        moves.push({
          from,
          to: jump.landing,
          capture: true,
          capturedSquare: jump.landing,
        });
      }
    }
  }

  return moves;
}

export function generateSoldierMoves(pos: Position): Move[] {
  const moves: Move[] = [];
  const occupied = occupiedSquares(pos);

  for (const from of pos.soldiers) {
    for (const to of orthogonalNeighbors(from)) {
      if (!occupied.has(to)) {
        moves.push({ from, to, capture: false, capturedSquare: -1 });
      }
    }
  }

  return moves;
}

export function generateLegalMoves(pos: Position): Move[] {
  return pos.side === "cannon" ? generateCannonMoves(pos) : generateSoldierMoves(pos);
}

export function applyMove(pos: Position, move: Move): Position {
  const next = clonePosition(pos);

  if (pos.side === "cannon") {
    next.cannons.delete(move.from);
    next.cannons.add(move.to);
    if (move.capture) {
      next.soldiers.delete(move.capturedSquare);
    }
  } else {
    next.soldiers.delete(move.from);
    next.soldiers.add(move.to);
  }

  next.side = opposite(pos.side);
  return next;
}

export function cannonHasAnyMove(pos: Position): boolean {
  return generateCannonMoves(pos).length > 0;
}

export function terminalOutcome(pos: Position): Outcome {
  if (pos.soldiers.size === 0) {
    return "CannonWin";
  }
  if (!cannonHasAnyMove(pos)) {
    return "SoldierWin";
  }
  if (generateLegalMoves(pos).length === 0) {
    return pos.side === "cannon" ? "SoldierWin" : "CannonWin";
  }
  return "Unknown";
}

function maskFromSquares(squares: Set<number>): number {
  let mask = 0;
  for (const square of squares) {
    mask |= 1 << square;
  }
  return mask;
}

export function packPositionKey(pos: Position): bigint {
  const cannonMask = maskFromSquares(pos.cannons);
  const soldierMask = maskFromSquares(pos.soldiers);
  const side = pos.side === "cannon" ? 0n : 1n;
  return BigInt(cannonMask) | (BigInt(soldierMask) << 25n) | (side << 50n);
}

export function positionToNotation(pos: Position): string {
  const rows: string[] = [];
  for (let row = 0; row < boardSize; row += 1) {
    let text = "";
    for (let col = 0; col < boardSize; col += 1) {
      const square = row * boardSize + col;
      if (pos.soldiers.has(square)) text += "S";
      else if (pos.cannons.has(square)) text += "C";
      else text += ".";
    }
    rows.push(text);
  }
  return `${rows.join("/")} ${pos.side === "cannon" ? "c" : "s"}`;
}
