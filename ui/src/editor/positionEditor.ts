import {
  boardSize,
  clonePosition,
  initialPosition,
  isValidSquare,
  parsePositionNotation,
  type Position,
  type Side,
} from "../engine";

export type BoardMode = "analysis" | "edit";
export type BoardEditTool = "select" | "placeCannon" | "placeSoldier" | "erase";

export const emptyPosition: Position = {
  cannons: new Set<number>(),
  soldiers: new Set<number>(),
  side: "cannon",
};

export const initialAfterDrawingMoveNotation = "SSSSS/SSSSS/SSCSS/...../.C.C. s";

export interface PositionPreset {
  id: string;
  label: string;
  position: Position;
}

export function positionWithSide(position: Position, side: Side): Position {
  const next = clonePosition(position);
  next.side = side;
  return next;
}

export function clearSoldiers(position: Position): Position {
  const next = clonePosition(position);
  next.soldiers.clear();
  return next;
}

export function clearCannons(position: Position): Position {
  const next = clonePosition(position);
  next.cannons.clear();
  return next;
}

export function applyEditTool(position: Position, square: number, tool: BoardEditTool): Position {
  const next = clonePosition(position);
  if (tool === "placeCannon") {
    next.soldiers.delete(square);
    if (next.cannons.has(square)) {
      next.cannons.delete(square);
    } else {
      next.cannons.add(square);
    }
  } else if (tool === "placeSoldier") {
    next.cannons.delete(square);
    if (next.soldiers.has(square)) {
      next.soldiers.delete(square);
    } else {
      next.soldiers.add(square);
    }
  } else if (tool === "erase") {
    next.cannons.delete(square);
    next.soldiers.delete(square);
  }
  return next;
}

export function movePieceFreely(position: Position, from: number, to: number): Position {
  const next = clonePosition(position);
  const movingCannon = next.cannons.has(from);
  const movingSoldier = next.soldiers.has(from);
  if (!movingCannon && !movingSoldier) {
    return next;
  }
  next.cannons.delete(from);
  next.soldiers.delete(from);
  next.cannons.delete(to);
  next.soldiers.delete(to);
  if (movingCannon) {
    next.cannons.add(to);
  } else if (movingSoldier) {
    next.soldiers.add(to);
  }
  return next;
}

export function randomLegalDensePosition(): Position {
  const squares = Array.from({ length: 25 }, (_, index) => index);
  for (let index = squares.length - 1; index > 0; index -= 1) {
    const other = Math.floor(Math.random() * (index + 1));
    [squares[index], squares[other]] = [squares[other], squares[index]];
  }
  const cannons = new Set(squares.slice(0, 3));
  const maxSoldiers = Math.min(15, 22);
  const soldierCount = Math.floor(Math.random() * (maxSoldiers + 1));
  const soldiers = new Set(squares.slice(3, 3 + soldierCount));
  return {
    cannons,
    soldiers,
    side: Math.random() < 0.5 ? "cannon" : "soldier",
  };
}

export function parseEditablePositionNotation(text: string): Position {
  try {
    return parsePositionNotation(text);
  } catch {
    // Fall through to the relaxed editor parser below.
  }

  const parts = text.trim().split(/\s+/u);
  if (parts.length !== 2) {
    throw new Error("Notation must contain a board and side, for example SSSSS/SSSSS/SSSSS/...../.CCC. c");
  }
  const [board, sideText] = parts;
  if (sideText !== "c" && sideText !== "s") {
    throw new Error("Side must be c or s");
  }

  const rows = board.split("/");
  if (rows.length !== boardSize) {
    throw new Error("Board notation must contain five rows");
  }

  const cannons = new Set<number>();
  const soldiers = new Set<number>();
  for (let row = 0; row < boardSize; row += 1) {
    if (rows[row].length !== boardSize) {
      throw new Error("Each board row must contain five cells");
    }
    for (let col = 0; col < boardSize; col += 1) {
      const square = row * boardSize + col;
      if (!isValidSquare(square)) {
        throw new Error("Board square is outside the 5x5 board");
      }
      const cell = rows[row][col];
      if (cell === "C") {
        cannons.add(square);
      } else if (cell === "S") {
        soldiers.add(square);
      } else if (cell !== ".") {
        throw new Error("Board cells must be C, S, or .");
      }
    }
  }

  return {
    cannons,
    soldiers,
    side: sideText === "c" ? "cannon" : "soldier",
  };
}

export function positionPresets(): PositionPreset[] {
  return [
    {
      id: "initial",
      label: "初始局面",
      position: initialPosition(),
    },
    {
      id: "after-drawing-move",
      label: "初始保和首着后",
      position: parseEditablePositionNotation(initialAfterDrawingMoveNotation),
    },
    {
      id: "empty-soldiers",
      label: "空兵局面",
      position: {
        cannons: new Set([21, 22, 23]),
        soldiers: new Set<number>(),
        side: "cannon",
      },
    },
    {
      id: "empty-board",
      label: "清空棋盘",
      position: clonePosition(emptyPosition),
    },
  ];
}
