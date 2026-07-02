export type BoardOrientation = "cannon-bottom" | "soldier-bottom";

const squareCount = 25;

export function displayIndexToSquare(displayIndex: number, orientation: BoardOrientation): number {
  return orientation === "cannon-bottom" ? displayIndex : squareCount - 1 - displayIndex;
}

export function squareToDisplayIndex(square: number, orientation: BoardOrientation): number {
  return orientation === "cannon-bottom" ? square : squareCount - 1 - square;
}
