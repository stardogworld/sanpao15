import { generateLegalMoves, applyMove, type Move, type Position, type Side } from "../engine";
import { formatMove, zh } from "../i18n/zh";
import type { MoveOutcomeInfo } from "../analysis/bestMove";

export interface MoveShape {
  title: string;
  subtitle: string;
  tags: string[];
}

const centerSquares = new Set([6, 7, 8, 11, 12, 13, 16, 17, 18]);
const edgeSquares = new Set([0, 1, 2, 3, 4, 5, 9, 10, 14, 15, 19, 20, 21, 22, 23, 24]);
const cornerSquares = new Set([0, 4, 20, 24]);

function row(square: number): number {
  return Math.floor(square / 5);
}

function column(square: number): number {
  return square % 5;
}

function centerDistance(square: number): number {
  return Math.abs(row(square) - 2) + Math.abs(column(square) - 2);
}

function adjacentToCannon(position: Position, square: number): boolean {
  for (const cannon of position.cannons) {
    if (Math.abs(row(cannon) - row(square)) + Math.abs(column(cannon) - column(square)) === 1) return true;
  }
  return false;
}

function cannonMobility(position: Position): number {
  let total = 0;
  for (const cannon of position.cannons) {
    total += generateLegalMoves({ ...position, side: "cannon" }).filter((move) => move.from === cannon).length;
  }
  return total;
}

function cannonMoveShape(move: Move): string {
  if (move.capture) {
    if (move.capturedSquare !== undefined && centerSquares.has(move.capturedSquare)) return zh.moveShape.hitCenter;
    if (move.capturedSquare !== undefined && edgeSquares.has(move.capturedSquare)) return zh.moveShape.hitEdge;
    return zh.moveShape.captureSoldier;
  }
  if (centerDistance(move.to) < centerDistance(move.from)) return zh.moveShape.moveCenter;
  if ((edgeSquares.has(move.from) || cornerSquares.has(move.from)) && centerDistance(move.to) < centerDistance(move.from)) {
    return zh.moveShape.leaveEdge;
  }
  return zh.moveShape.repositionCannon;
}

function soldierMoveShape(position: Position, move: Move): string {
  const before = cannonMobility(position);
  const after = cannonMobility(applyMove(position, move));
  if (after < before) return zh.moveShape.blockEscape;
  if (adjacentToCannon(position, move.to)) return zh.moveShape.pressureCannon;
  if (centerSquares.has(move.to)) return zh.moveShape.buildWall;
  return zh.moveShape.moveSoldier;
}

export function describeMoveShape(position: Position, move: Move, side: Side, info?: MoveOutcomeInfo): MoveShape {
  const tags = [
    info?.successorOutcome ? zh.outcome[info.successorOutcome] : null,
    move.capture ? zh.moveShape.captureTag : null,
  ].filter((tag): tag is string => typeof tag === "string");
  return {
    title: side === "cannon" ? cannonMoveShape(move) : soldierMoveShape(position, move),
    subtitle: `记法：${formatMove(move)}`,
    tags,
  };
}
