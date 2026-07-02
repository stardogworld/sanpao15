import type { Position, Side } from "../engine";

export const standardRulesetHash = 0x5331355f76325f04n;

const squareCount = 25;

const binomTable: bigint[][] = Array.from({ length: 26 }, () => Array<bigint>(26).fill(0n));
for (let n = 0; n <= 25; n += 1) {
  binomTable[n][0] = 1n;
  binomTable[n][n] = 1n;
  for (let k = 1; k < n; k += 1) {
    binomTable[n][k] = binomTable[n - 1][k - 1] + binomTable[n - 1][k];
  }
}

export function binom(n: number, k: number): bigint {
  if (n < 0 || n > 25 || k < 0 || k > n) {
    return 0n;
  }
  return binomTable[n][k];
}

function sideRank(side: Side): bigint {
  return side === "cannon" ? 0n : 1n;
}

function sortedSquares(squares: Set<number>): number[] {
  return Array.from(squares).sort((left, right) => left - right);
}

export function rankCombinationFromSquares(squares: number[], n: number, k: number): bigint {
  if (squares.length !== k) {
    throw new Error(`Expected ${k} squares, got ${squares.length}`);
  }
  let rank = 0n;
  for (let chosen = 1; chosen <= k; chosen += 1) {
    const bit = squares[chosen - 1];
    if (bit < 0 || bit >= n) {
      throw new Error("Combination square is outside rank domain");
    }
    rank += binom(bit, chosen);
  }
  return rank;
}

export function compressSoldiersToAvailable(soldiers: Set<number>, cannons: Set<number>): number[] {
  const compressed: number[] = [];
  let localIndex = 0;
  for (let square = 0; square < squareCount; square += 1) {
    if (cannons.has(square)) {
      continue;
    }
    if (soldiers.has(square)) {
      compressed.push(localIndex);
    }
    localIndex += 1;
  }
  return compressed;
}

export function denseStateCount(soldierCount: number): bigint {
  if (soldierCount < 0 || soldierCount > 15) {
    throw new Error("Soldier count must be in 0..15");
  }
  return binom(25, 3) * binom(22, soldierCount) * 2n;
}

export function denseIndex(position: Position): bigint {
  if (position.cannons.size !== 3) {
    throw new Error("Dense tablebase positions must contain exactly three cannons");
  }
  for (const square of position.cannons) {
    if (position.soldiers.has(square)) {
      throw new Error("Cannons and soldiers overlap");
    }
  }

  const soldierCount = position.soldiers.size;
  const cannonRank = rankCombinationFromSquares(sortedSquares(position.cannons), 25, 3);
  const soldierRank = rankCombinationFromSquares(
    compressSoldiersToAvailable(position.soldiers, position.cannons),
    22,
    soldierCount,
  );
  return ((cannonRank * binom(22, soldierCount)) + soldierRank) * 2n + sideRank(position.side);
}
