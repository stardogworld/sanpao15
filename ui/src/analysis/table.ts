import { applyMove, generateLegalMoves, packPositionKey, type Move, type Outcome, type Position } from "../engine";

export interface TableEntry {
  key: bigint;
  outcome: Outcome;
  distance: number;
  bestMove: Move | null;
}

export interface ResultTable {
  exact: boolean;
  truncated: boolean;
  stateCount: bigint;
  initialKey: bigint;
  rulesetHash: bigint;
  entries: Map<bigint, TableEntry>;
  lookup(key: bigint): TableEntry | undefined;
}

export interface MoveTableAnalysis {
  move: Move;
  outcome: Outcome;
  distance: number;
  isBest: boolean;
}

export interface PositionTableAnalysis {
  foundInTable: boolean;
  outcome: Outcome;
  distance: number;
  bestMove: Move | null;
  legalMoves: MoveTableAnalysis[];
}

const tableMagic = "S15TBL1\0";
const headerSize = 40;
const entrySize = 16;

function outcomeFromByte(value: number): Outcome {
  switch (value) {
    case 0:
      return "Unknown";
    case 1:
      return "CannonWin";
    case 2:
      return "SoldierWin";
    case 3:
      return "Draw";
    default:
      throw new Error(`Invalid table outcome: ${value}`);
  }
}

function sameMove(left: Move, right: Move): boolean {
  return (
    left.from === right.from &&
    left.to === right.to &&
    left.capture === right.capture &&
    left.capturedSquare === right.capturedSquare
  );
}

function readMagic(view: DataView): string {
  let text = "";
  for (let i = 0; i < 8; i += 1) {
    text += String.fromCharCode(view.getUint8(i));
  }
  return text;
}

export async function loadResultTable(url = "/tables/standard.s15tbl"): Promise<ResultTable> {
  const response = await fetch(url);
  if (!response.ok) {
    throw new Error("未找到结果表。请先用 CLI 生成表文件，并放入 ui/public/tables/standard.s15tbl。");
  }

  const buffer = await response.arrayBuffer();
  const view = new DataView(buffer);
  if (view.byteLength < headerSize || readMagic(view) !== tableMagic) {
    throw new Error("结果表格式无效。");
  }

  const version = view.getUint32(8, true);
  if (version !== 1) {
    throw new Error(`不支持的结果表版本：${version}`);
  }

  const flags = view.getUint32(12, true);
  const stateCount = view.getBigUint64(16, true);
  const initialKey = view.getBigUint64(24, true);
  const rulesetHash = view.getBigUint64(32, true);
  const expectedSize = headerSize + Number(stateCount) * entrySize;
  if (view.byteLength < expectedSize) {
    throw new Error("结果表文件不完整。");
  }

  const entries = new Map<bigint, TableEntry>();
  let offset = headerSize;
  for (let index = 0n; index < stateCount; index += 1n) {
    const key = view.getBigUint64(offset, true);
    offset += 8;
    const outcome = outcomeFromByte(view.getUint8(offset));
    offset += 1;
    const distance = view.getInt32(offset, true);
    offset += 4;
    const bestFrom = view.getInt8(offset);
    offset += 1;
    const bestTo = view.getInt8(offset);
    offset += 1;
    const bestCapturedSquare = view.getInt8(offset);
    offset += 1;
    const entryFlags = view.getUint8(offset);
    offset += 1;

    const hasBestMove = (entryFlags & 1) !== 0;
    const isCapture = (entryFlags & 2) !== 0;
    const bestMove = hasBestMove
      ? {
          from: bestFrom,
          to: bestTo,
          capture: isCapture,
          capturedSquare: isCapture ? bestCapturedSquare : -1,
        }
      : null;

    entries.set(key, { key, outcome, distance, bestMove });
  }

  return {
    exact: (flags & 1) !== 0,
    truncated: (flags & 2) !== 0,
    stateCount,
    initialKey,
    rulesetHash,
    entries,
    lookup(key: bigint) {
      return entries.get(key);
    },
  };
}

export function analyzePositionWithTable(position: Position, table: ResultTable): PositionTableAnalysis {
  const current = table.lookup(packPositionKey(position));
  const bestMove = current?.bestMove ?? null;
  const legalMoves = generateLegalMoves(position).map((move) => {
    const next = applyMove(position, move);
    const nextEntry = table.lookup(packPositionKey(next));
    return {
      move,
      outcome: nextEntry?.outcome ?? "Unknown",
      distance: nextEntry?.distance ?? -1,
      isBest: bestMove !== null && sameMove(move, bestMove),
    };
  });

  return {
    foundInTable: current !== undefined,
    outcome: current?.outcome ?? "Unknown",
    distance: current?.distance ?? -1,
    bestMove,
    legalMoves,
  };
}
