import type { Outcome, Position } from "../engine";
import { denseIndex, denseStateCount, standardRulesetHash } from "./denseIndex";

export type DenseResultEncoding = "byte" | "2bit";

export interface DenseLayerHandle {
  soldierCount: number;
  stateCount: bigint;
  encoding: DenseResultEncoding;
  payloadOffset: number;
  payloadBytes: bigint;
  file: File;
}

export interface TablebaseDirectory {
  layers: Map<number, DenseLayerHandle>;
  rulesetHash: bigint;
  sourceName: string;
}

export interface TablebaseLookupResult {
  soldierCount: number;
  denseIndex: bigint;
  outcome: Outcome;
}

interface FileSystemDirectoryHandle {
  name: string;
  values(): AsyncIterable<unknown>;
}

interface FileSystemFileHandle {
  kind: "file";
  name: string;
  getFile(): Promise<File>;
}

const denseMagic = "S15RES1\0";
const headerBytes = 44;

function readMagic(view: DataView): string {
  let text = "";
  for (let index = 0; index < 8; index += 1) {
    text += String.fromCharCode(view.getUint8(index));
  }
  return text;
}

function decodeEncoding(value: number): DenseResultEncoding {
  if (value === 1) return "byte";
  if (value === 2) return "2bit";
  throw new Error(`Unsupported .s15res encoding: ${value}`);
}

function decodeOutcome(value: number): Outcome {
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
      throw new Error(`Invalid dense outcome value: ${value}`);
  }
}

function layerFileName(soldierCount: number): string {
  return `layer-${String(soldierCount).padStart(2, "0")}.s15res`;
}

async function parseDenseLayer(file: File): Promise<DenseLayerHandle> {
  const header = await file.slice(0, headerBytes).arrayBuffer();
  if (header.byteLength !== headerBytes) {
    throw new Error(`${file.name} is too small to be a .s15res layer`);
  }

  const view = new DataView(header);
  if (readMagic(view) !== denseMagic) {
    throw new Error(`${file.name} has invalid .s15res magic`);
  }
  const version = view.getUint32(8, true);
  if (version !== 1) {
    throw new Error(`${file.name} uses unsupported .s15res version ${version}`);
  }
  const rulesetHash = view.getBigUint64(12, true);
  if (rulesetHash !== standardRulesetHash) {
    throw new Error(`${file.name} ruleset hash does not match sanpao15-min-four-soldiers`);
  }
  const soldierCount = view.getUint32(20, true);
  if (soldierCount > 15) {
    throw new Error(`${file.name} soldier count is outside 0..15`);
  }
  const stateCount = view.getBigUint64(24, true);
  const encoding = decodeEncoding(view.getUint32(32, true));
  const payloadBytes = view.getBigUint64(36, true);
  const expectedStates = denseStateCount(soldierCount);
  if (stateCount !== expectedStates) {
    throw new Error(`${file.name} state count does not match layer ${soldierCount}`);
  }
  const expectedPayload = encoding === "byte" ? stateCount : (stateCount + 3n) / 4n;
  if (payloadBytes !== expectedPayload) {
    throw new Error(`${file.name} payload size does not match ${encoding} encoding`);
  }
  if (BigInt(file.size) !== BigInt(headerBytes) + payloadBytes) {
    throw new Error(`${file.name} file size does not match header`);
  }

  return {
    soldierCount,
    stateCount,
    encoding,
    payloadOffset: headerBytes,
    payloadBytes,
    file,
  };
}

async function buildTablebaseDirectory(files: File[], sourceName: string): Promise<TablebaseDirectory> {
  const layers = new Map<number, DenseLayerHandle>();
  const errors: string[] = [];

  for (const file of files) {
    if (!/^layer-\d\d\.s15res$/u.test(file.name)) {
      continue;
    }
    try {
      const layer = await parseDenseLayer(file);
      if (file.name !== layerFileName(layer.soldierCount)) {
        throw new Error(`${file.name} does not match header soldier count ${layer.soldierCount}`);
      }
      layers.set(layer.soldierCount, layer);
    } catch (error) {
      errors.push(error instanceof Error ? error.message : String(error));
    }
  }

  if (errors.length > 0) {
    throw new Error(errors.join("\n"));
  }
  if (layers.size === 0) {
    throw new Error("No layer-XX.s15res files were selected");
  }

  return { layers, rulesetHash: standardRulesetHash, sourceName };
}

async function openWithDirectoryPicker(): Promise<TablebaseDirectory> {
  const picker = (window as unknown as {
    showDirectoryPicker?: () => Promise<FileSystemDirectoryHandle>;
  }).showDirectoryPicker;
  if (!picker) {
    throw new Error("Directory picker is not available");
  }

  const directory = await picker();
  const files: File[] = [];
  for await (const entry of directory.values()) {
    const handle = entry as Partial<FileSystemFileHandle>;
    if (handle.kind === "file" && handle.name?.endsWith(".s15res") && handle.getFile) {
      files.push(await handle.getFile());
    }
  }
  return buildTablebaseDirectory(files, directory.name);
}

async function openWithFileInput(directoryMode: boolean): Promise<TablebaseDirectory> {
  return new Promise((resolve, reject) => {
    const input = document.createElement("input");
    input.type = "file";
    input.multiple = true;
    input.accept = ".s15res";
    input.style.display = "none";
    if (directoryMode) {
      input.setAttribute("webkitdirectory", "");
    }
    input.addEventListener("change", async () => {
      try {
        const files = Array.from(input.files ?? []);
        input.remove();
        resolve(await buildTablebaseDirectory(files, "所选分层文件"));
      } catch (error) {
        input.remove();
        reject(error);
      }
    });
    input.addEventListener("cancel", () => {
      input.remove();
      reject(new Error("Tablebase selection was cancelled"));
    });
    document.body.append(input);
    input.click();
  });
}

export async function openTablebaseDirectory(): Promise<TablebaseDirectory> {
  try {
    return await openWithDirectoryPicker();
  } catch (error) {
    if (error instanceof DOMException && error.name === "AbortError") {
      throw error;
    }
    return openWithFileInput(true);
  }
}

export async function openTablebaseFiles(): Promise<TablebaseDirectory> {
  return openWithFileInput(false);
}

export async function lookupOutcome(
  tablebase: TablebaseDirectory,
  position: Position,
): Promise<TablebaseLookupResult> {
  const soldierCount = position.soldiers.size;
  const layer = tablebase.layers.get(soldierCount);
  if (!layer) {
    throw new Error(`Missing ${layerFileName(soldierCount)}`);
  }

  const index = denseIndex(position);
  if (index >= layer.stateCount) {
    throw new Error("Position dense index is outside the layer");
  }

  const payloadIndex = layer.encoding === "byte" ? index : index / 4n;
  if (payloadIndex >= layer.payloadBytes) {
    throw new Error("Dense result payload offset is outside the layer file");
  }
  if (payloadIndex > BigInt(Number.MAX_SAFE_INTEGER - layer.payloadOffset)) {
    throw new Error("Dense result offset exceeds browser File.slice precision");
  }

  const byteOffset = layer.payloadOffset + Number(payloadIndex);
  const buffer = await layer.file.slice(byteOffset, byteOffset + 1).arrayBuffer();
  if (buffer.byteLength !== 1) {
    throw new Error(`Unable to read outcome byte from ${layer.file.name}`);
  }
  const byte = new DataView(buffer).getUint8(0);
  const value =
    layer.encoding === "byte" ? byte : (byte >> Number((index % 4n) * 2n)) & 0x03;

  return {
    soldierCount,
    denseIndex: index,
    outcome: decodeOutcome(value),
  };
}
