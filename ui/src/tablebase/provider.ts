import { parsePositionNotation, positionToNotation, type Move, type Outcome, type Position, type Side } from "../engine";
import { summarizeRecommendation, type PositionAnalysisResult } from "../analysis/bestMove";
import type { SideComparisonResult } from "../analysis/sideComparison";
import { validatePosition, type PositionValidation } from "../editor/validation";
import {
  lookupOutcome,
  type TablebaseDirectory,
  type TablebaseLookupResult,
} from "./denseResult";
import {
  recommendMoves,
  type MoveClassification,
  type RecommendedMove,
  type TablebaseRecommendationResult,
} from "./recommend";
import { exploreWdlLine, type WdlLineExplorerResult } from "./lineExplorer";

export interface TablebaseStatus {
  ok: boolean;
  mode: "local-backend" | "browser-files";
  tablebaseLoaded: boolean;
  complete: boolean;
  sourceName: string;
  loadedLayers: number[];
  missingLayers: number[];
  invalidLayers?: Array<{ soldierCount: number; path: string; error: string }>;
  rulesetName?: string;
  rulesetHash?: string;
  encoding: string;
  readMode: string;
  error?: string;
  code?: string;
}

export interface TablebaseProvider {
  kind: "backend" | "browser-files";
  status(): Promise<TablebaseStatus>;
  validate(position: Position): PositionValidation;
  query(position: Position): Promise<TablebaseLookupResult>;
  recommend(position: Position): Promise<TablebaseRecommendationResult>;
  analyze(position: Position): Promise<PositionAnalysisResult>;
  compareSides(position: Position): Promise<SideComparisonResult>;
  explore(position: Position, maxPlies: number): Promise<WdlLineExplorerResult>;
}

function statusFromDirectory(tablebase: TablebaseDirectory): TablebaseStatus {
  const layers = Array.from(tablebase.layers.keys()).sort((left, right) => left - right);
  const encodings = Array.from(new Set(Array.from(tablebase.layers.values()).map((layer) => layer.encoding))).join(", ");
  const missingLayers = Array.from({ length: 16 }, (_, index) => index).filter((layer) => !tablebase.layers.has(layer));
  return {
    ok: true,
    mode: "browser-files",
    tablebaseLoaded: true,
    complete: missingLayers.length === 0,
    sourceName: tablebase.sourceName,
    loadedLayers: layers,
    missingLayers,
    rulesetHash: `0x${tablebase.rulesetHash.toString(16).toUpperCase()}`,
    encoding: encodings || "unknown",
    readMode: "browser-random-read",
  };
}

export class BrowserFileTablebaseProvider implements TablebaseProvider {
  readonly kind = "browser-files" as const;

  constructor(readonly tablebase: TablebaseDirectory) {}

  async status(): Promise<TablebaseStatus> {
    return statusFromDirectory(this.tablebase);
  }

  validate(position: Position): PositionValidation {
    return validatePosition(position, {
      hasLayer: (soldierCount) => this.tablebase.layers.has(soldierCount),
    });
  }

  async query(position: Position): Promise<TablebaseLookupResult> {
    return lookupOutcome(this.tablebase, position);
  }

  async recommend(position: Position): Promise<TablebaseRecommendationResult> {
    return recommendMoves(this.tablebase, position);
  }

  async analyze(position: Position): Promise<PositionAnalysisResult> {
    const validation = this.validate(position);
    if (!validation.canQuery) {
      return { validation, recommendation: null, summary: null };
    }
    try {
      const recommendation = await this.recommend(position);
      return {
        validation,
        recommendation,
        summary: summarizeRecommendation(recommendation, position.side),
      };
    } catch (error) {
      return {
        validation: { ...validation, canQuery: false, reason: error instanceof Error ? error.message : String(error) },
        recommendation: null,
        summary: null,
        error: error instanceof Error ? error.message : String(error),
      };
    }
  }

  async compareSides(position: Position): Promise<SideComparisonResult> {
    const cannon = await this.analyze({ ...position, side: "cannon" });
    const soldier = await this.analyze({ ...position, side: "soldier" });
    return {
      cannon: {
        side: "cannon",
        validation: cannon.validation,
        summary: cannon.summary,
        error: cannon.error,
      },
      soldier: {
        side: "soldier",
        validation: soldier.validation,
        summary: soldier.summary,
        error: soldier.error,
      },
    };
  }

  async explore(position: Position, maxPlies: number): Promise<WdlLineExplorerResult> {
    return exploreWdlLine(this.tablebase, position, maxPlies);
  }
}

interface BackendStatusResponse {
  ok: boolean;
  mode: "local-backend";
  tablebaseLoaded: boolean;
  tablebaseDir?: string;
  rulesetName?: string;
  rulesetHash?: string;
  complete: boolean;
  loadedLayers?: number[];
  missingLayers?: number[];
  invalidLayers?: Array<{ soldierCount: number; path: string; error: string }>;
  encoding?: string;
  readMode?: string;
  error?: string;
  code?: string;
}

interface BackendMoveResponse {
  move: string;
  from: number;
  to: number;
  capture: boolean;
  capturedSquare: number;
  successorPosition: string;
  successorSoldierCount: number;
  successorIndex: string;
  successorOutcome: Outcome;
  classification: MoveClassification;
}

interface BackendRecommendResponse {
  ok: boolean;
  position: string;
  outcome: Outcome;
  soldierCount: number;
  denseIndex: string;
  legalMoveCount: number;
  recommendedMoves: BackendMoveResponse[];
  moves: BackendMoveResponse[];
  error?: string;
}

interface BackendLineResponse {
  ok: boolean;
  start: string;
  startOutcome: Outcome;
  stopReason: WdlLineExplorerResult["stopReason"];
  cycleStartPly: number | null;
  error?: string;
  plies: Array<{
    ply: number;
    position: string;
    sideToMove: Side;
    outcome: Outcome;
    soldierCount: number;
    denseIndex: string;
    chosenMove: string;
    from: number;
    to: number;
    capture: boolean;
    capturedSquare: number;
    successorPosition: string;
    successorOutcome: Outcome;
    classification: MoveClassification;
    alternatives: Array<{
      from: number;
      to: number;
      capture: boolean;
      capturedSquare: number;
      successorOutcome: Outcome;
      classification: MoveClassification;
    }>;
  }>;
}

function moveFromBackend(move: BackendMoveResponse | { from: number; to: number; capture: boolean; capturedSquare: number }): Move {
  return {
    from: move.from,
    to: move.to,
    capture: move.capture,
    capturedSquare: move.capturedSquare,
  };
}

function recommendedMoveFromBackend(move: BackendMoveResponse): RecommendedMove {
  return {
    move: moveFromBackend(move),
    successor: parsePositionNotation(move.successorPosition),
    successorSoldierCount: move.successorSoldierCount,
    successorIndex: BigInt(move.successorIndex),
    successorOutcome: move.successorOutcome,
    classification: move.classification,
  };
}

function recommendationFromBackend(response: BackendRecommendResponse): TablebaseRecommendationResult {
  const moves = response.moves.map(recommendedMoveFromBackend);
  const recommendedMoves = response.recommendedMoves.map(recommendedMoveFromBackend);
  return {
    soldierCount: response.soldierCount,
    denseIndex: BigInt(response.denseIndex),
    outcome: response.outcome,
    moves,
    recommendedMoves,
  };
}

function lineFromBackend(response: BackendLineResponse): WdlLineExplorerResult {
  return {
    start: parsePositionNotation(response.start),
    startOutcome: response.startOutcome,
    stopReason: response.stopReason,
    cycleStartPly: response.cycleStartPly,
    error: response.error,
    plies: response.plies.map((ply) => ({
      ply: ply.ply,
      position: parsePositionNotation(ply.position),
      soldierCount: ply.soldierCount,
      denseIndex: BigInt(ply.denseIndex),
      outcome: ply.outcome,
      sideToMove: ply.sideToMove,
      chosenMove: moveFromBackend({
        from: ply.from,
        to: ply.to,
        capture: ply.capture,
        capturedSquare: ply.capturedSquare,
      }),
      successor: parsePositionNotation(ply.successorPosition),
      successorOutcome: ply.successorOutcome,
      classification: ply.classification,
      alternatives: ply.alternatives.map((alternative) => ({
        move: moveFromBackend(alternative),
        successorOutcome: alternative.successorOutcome,
        classification: alternative.classification,
      })),
    })),
  };
}

async function fetchJson<T>(url: string, init?: RequestInit): Promise<T> {
  const response = await fetch(url, init);
  const data = await response.json() as T & { error?: string };
  if (!response.ok) {
    throw new Error(data.error ?? `HTTP ${response.status}`);
  }
  return data;
}

export class BackendTablebaseProvider implements TablebaseProvider {
  readonly kind = "backend" as const;

  constructor(private readonly statusSnapshot: BackendStatusResponse) {}

  async status(): Promise<TablebaseStatus> {
    return tablebaseStatusFromBackend(this.statusSnapshot);
  }

  validate(position: Position): PositionValidation {
    const loaded = new Set(this.statusSnapshot.loadedLayers ?? []);
    return validatePosition(position, {
      hasLayer: (layer) => loaded.has(layer),
    });
  }

  async query(position: Position): Promise<TablebaseLookupResult> {
    const response = await fetchJson<{ ok: boolean; soldierCount: number; denseIndex: string; outcome: Outcome }>("/api/query", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ position: positionToNotation(position) }),
    });
    return {
      soldierCount: response.soldierCount,
      denseIndex: BigInt(response.denseIndex),
      outcome: response.outcome,
    };
  }

  async recommend(position: Position): Promise<TablebaseRecommendationResult> {
    const response = await fetchJson<BackendRecommendResponse>("/api/recommend", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ position: positionToNotation(position) }),
    });
    return recommendationFromBackend(response);
  }

  async analyze(position: Position): Promise<PositionAnalysisResult> {
    const validation = this.validate(position);
    if (!validation.canQuery) {
      return { validation, recommendation: null, summary: null };
    }
    try {
      const recommendation = await this.recommend(position);
      return {
        validation,
        recommendation,
        summary: summarizeRecommendation(recommendation, position.side),
      };
    } catch (error) {
      return {
        validation: { ...validation, canQuery: false, reason: error instanceof Error ? error.message : String(error) },
        recommendation: null,
        summary: null,
        error: error instanceof Error ? error.message : String(error),
      };
    }
  }

  async compareSides(position: Position): Promise<SideComparisonResult> {
    const [cannon, soldier] = await Promise.all([
      this.analyze({ ...position, side: "cannon" }),
      this.analyze({ ...position, side: "soldier" }),
    ]);
    return {
      cannon: {
        side: "cannon",
        validation: cannon.validation,
        summary: cannon.summary,
        error: cannon.error,
      },
      soldier: {
        side: "soldier",
        validation: soldier.validation,
        summary: soldier.summary,
        error: soldier.error,
      },
    };
  }

  async explore(position: Position, maxPlies: number): Promise<WdlLineExplorerResult> {
    const response = await fetchJson<BackendLineResponse>("/api/explore", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ position: positionToNotation(position), maxPlies }),
    });
    return lineFromBackend(response);
  }
}

function tablebaseStatusFromBackend(status: BackendStatusResponse): TablebaseStatus {
  return {
    ok: status.ok,
    mode: "local-backend",
    tablebaseLoaded: status.tablebaseLoaded,
    complete: status.complete,
    sourceName: status.tablebaseDir ?? "local backend",
    loadedLayers: status.loadedLayers ?? [],
    missingLayers: status.missingLayers ?? [],
    invalidLayers: status.invalidLayers,
    rulesetName: status.rulesetName,
    rulesetHash: status.rulesetHash,
    encoding: status.encoding ?? "unknown",
    readMode: status.readMode ?? "backend-random-read",
    error: status.error,
    code: status.code,
  };
}

async function fetchBackendStatus(timeoutMs: number): Promise<BackendStatusResponse | null> {
  const controller = new AbortController();
  const timeout = window.setTimeout(() => controller.abort(), timeoutMs);
  try {
    return await fetchJson<BackendStatusResponse>("/api/status", {
      signal: controller.signal,
      cache: "no-store",
    });
  } catch {
    return null;
  } finally {
    window.clearTimeout(timeout);
  }
}

export async function detectBackendStatus(timeoutMs = 800): Promise<TablebaseStatus | null> {
  const status = await fetchBackendStatus(timeoutMs);
  return status?.mode === "local-backend" ? tablebaseStatusFromBackend(status) : null;
}

export async function detectBackendProvider(timeoutMs = 800): Promise<BackendTablebaseProvider | null> {
  const status = await fetchBackendStatus(timeoutMs);
  if (status?.mode === "local-backend" && status.tablebaseLoaded && status.complete) {
    return new BackendTablebaseProvider(status);
  }
  return null;
}
