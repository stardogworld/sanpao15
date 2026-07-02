import type { Position } from "../engine";
import type { TablebaseDirectory } from "../tablebase/denseResult";
import { denseIndex } from "../tablebase/denseIndex";

export interface PositionValidation {
  ok: boolean;
  canQuery: boolean;
  cannonCount: number;
  soldierCount: number;
  reason?: string;
  denseIndex?: bigint;
  layerName?: string;
}

export function layerFileName(soldierCount: number): string {
  return `layer-${String(soldierCount).padStart(2, "0")}.s15res`;
}

export function validatePosition(
  position: Position,
  tablebase: TablebaseDirectory | null,
): PositionValidation {
  const cannonCount = position.cannons.size;
  const soldierCount = position.soldiers.size;

  if (cannonCount !== 3) {
    return {
      ok: false,
      canQuery: false,
      cannonCount,
      soldierCount,
      reason: `当前炮数为 ${cannonCount}。完整表库要求正好 3 个炮。`,
    };
  }

  if (soldierCount < 0 || soldierCount > 15) {
    return {
      ok: false,
      canQuery: false,
      cannonCount,
      soldierCount,
      reason: `当前兵数为 ${soldierCount}。完整表库最多支持 15 个兵。`,
    };
  }

  for (const square of position.cannons) {
    if (position.soldiers.has(square)) {
      return {
        ok: false,
        canQuery: false,
        cannonCount,
        soldierCount,
        reason: "炮和兵不能重叠。",
      };
    }
  }

  let index: bigint;
  try {
    index = denseIndex(position);
  } catch (error) {
    return {
      ok: false,
      canQuery: false,
      cannonCount,
      soldierCount,
      reason: error instanceof Error ? error.message : String(error),
    };
  }

  if (!tablebase) {
    return {
      ok: true,
      canQuery: false,
      cannonCount,
      soldierCount,
      denseIndex: index,
      layerName: layerFileName(soldierCount),
      reason: "表库未加载，请先选择目录或分层文件。",
    };
  }

  if (!tablebase.layers.has(soldierCount)) {
    return {
      ok: true,
      canQuery: false,
      cannonCount,
      soldierCount,
      denseIndex: index,
      layerName: layerFileName(soldierCount),
      reason: `缺少 ${layerFileName(soldierCount)}，无法查询 ${soldierCount} 兵局面。`,
    };
  }

  return {
    ok: true,
    canQuery: true,
    cannonCount,
    soldierCount,
    denseIndex: index,
    layerName: layerFileName(soldierCount),
  };
}
