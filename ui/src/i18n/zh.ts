import type { Move, Outcome, Side } from "../engine";
import type { MoveClassification } from "../tablebase/recommend";

export const zh = {
  appTitle: "三炮十五兵",
  appSubtitle: "胜负和表库分析器",
  rulesetBadge: "规则集：最少四兵",

  panels: {
    game: "对局",
    tablebase: "表库",
    lineExplorer: "路线探索",
    initialPosition: "初始局面",
    help: "规则说明",
    reachableAnalysis: "可达状态分析",
  },

  labels: {
    turn: "轮到",
    soldiers: "兵数",
    terminal: "终局",
    position: "局面",
    result: "结果",
    status: "状态",
    source: "来源",
    layers: "分层",
    encoding: "编码",
    ruleset: "规则集",
    readMode: "读取方式",
    outcome: "结果",
    denseIndex: "密集索引",
    legalMoves: "合法着法",
    recommended: "推荐着法",
    start: "起点",
    stop: "停止原因",
    cycle: "循环",
    error: "错误",
    plies: "手数",
    noLine: "尚未探索路线。",
    activePly: "当前手数",
    autoplay: "自动播放",
    on: "开启",
    off: "关闭",
    none: "无",
    maxPlies: "最多手数",
  },

  actions: {
    reset: "重置",
    undo: "悔棋",
    redo: "重做",
    analyze: "分析当前局面",
    copy: "复制",
    paste: "粘贴",
    directory: "选择目录",
    layerFiles: "选择分层文件",
    query: "查询",
    exploreLine: "探索路线",
    previous: "上一步",
    next: "下一步",
    auto: "自动播放",
    resetInitial: "重置到初始局面",
    showDrawingMove: "高亮保和首着",
    exploreDrawingLine: "探索保和路线",
    copyInitial: "复制初始局面",
  },

  placeholders: {
    pasteNotation: "粘贴局面记法",
  },

  outcome: {
    CannonWin: "炮胜",
    SoldierWin: "兵胜",
    Draw: "和棋",
    Unknown: "未知",
  } satisfies Record<Outcome, string>,

  side: {
    cannon: "炮方",
    soldier: "兵方",
  } satisfies Record<Side, string>,

  piece: {
    cannon: "炮",
    soldier: "兵",
    empty: "空格",
  },

  classification: {
    winning: "保持胜势",
    drawing: "保持和棋",
    losing: "走向败局",
  } satisfies Record<MoveClassification, string>,

  stopReason: {
    terminal: "到达终局",
    cycle: "检测到循环",
    maxPlies: "达到手数上限",
    noLegalMoves: "无合法着法",
    missingTablebase: "表库未加载或缺失",
    lookupError: "查询出错",
  },

  tablebase: {
    notLoadedBadge: "表库：未加载",
    loadedBadge: (count: number) => `表库：已加载 ${count}/16`,
    notLoadedTitle: "表库未加载",
    notLoadedDetail: "请选择目录或分层文件后查询。浏览器只随机读取需要的 .s15res 字节，不会全量加载表库。",
    noLookupTitle: "尚未查询",
    noLookupDetail: "请先加载表库；查询时只读取当前局面和合法后继所需的结果字节。",
    complete: "已完整加载 0..15",
    partial: "部分加载",
    randomRead: "随机读取 .s15res，不全量加载",
    selectingDirectory: "正在选择表库目录...",
    selectingFiles: "正在选择分层文件...",
    reading: "正在读取表库字节...",
    loaded: "表库已加载。",
    filesLoaded: "分层文件已加载。",
    lookupUpdated: (outcome: Outcome) => `查询完成：${outcomeText(outcome)}。`,
    lookupFailed: "表库查询失败。",
  },

  line: {
    note: "当前是胜负和表库，只保证胜、负、和结果；不包含最快胜、最短保和或距离信息。",
    maxPliesNote: "这是一条胜负和示例路线，不代表最快胜或最短保和。",
    exploring: "正在探索胜负和路线...",
    explored: (reason: string) => `路线探索结束：${stopReasonText(reason)}。`,
    alternatives: (count: number) => `${count} 个备选着法`,
    cells: {
      ply: "手数",
      side: "走子方",
      move: "着法",
      successor: "后继",
      class: "类型",
      soldiers: "兵数",
    },
  },

  initial: {
    title: "初始局面结论",
    position: "初始局面",
    result: "和棋",
    onlyMove: "唯一保和首着",
    onlyMoveText: "22->12 吃 12",
    meaning:
      "炮方第一手只有这一着可以保和：中炮跳吃中央兵。其他合法首着都会进入兵胜。",
    highlighted: "已高亮保和首着：22->12 吃 12。",
    copied: "已复制初始局面。",
    restored: "已重置到初始局面。",
  },

  feedback: {
    copied: "已复制局面。",
    pasted: "已粘贴局面。",
    moved: (move: string) => `已走：${move}。`,
    terminal: "当前为终局，不能继续走子。",
    selectOwnPiece: "请选择当前走子方的棋子。",
    noLegalMove: "该棋子没有合法着法。",
    legalTargets: (count: number) => `${count} 个合法目标。`,
    tablebaseFirst: "请先加载表库。",
  },

  analysis: {
    loading: "正在加载可达结果表...",
    tableNotLoaded: "结果表未加载。",
    legalMoves: "合法着法",
    title: "可达状态分析",
    table: "结果表",
    current: "当前局面",
    distance: "距离",
    suggestedMove: "建议着法",
    exact: "精确",
    truncated: "截断",
    partial: "部分",
    found: "已找到",
    notFound: "未找到",
    none: "无",
    suggested: "推荐",
  },

  help: [
    "棋盘为 5x5。炮方有 3 个炮，兵方最多有 15 个兵。",
    "炮可以横竖移动一格到空位。",
    "炮可以隔一个空格跳吃一个兵。",
    "兵可以横竖移动一格到空位，兵不能吃炮。",
    "兵数少于 4 时，炮胜。",
    "炮无合法着法时，兵胜。",
    "其他局面由完整表库给出炮胜、兵胜或和棋。",
    "当前表库只保存胜负和结果，不保存最快胜或最短保和步数。",
  ],
};

export function outcomeText(outcome: Outcome): string {
  return zh.outcome[outcome];
}

export function sideText(side: Side): string {
  return zh.side[side];
}

export function classificationText(classification: MoveClassification): string {
  return zh.classification[classification];
}

export function stopReasonText(reason: string): string {
  return zh.stopReason[reason as keyof typeof zh.stopReason] ?? reason;
}

export function formatMove(move: Move): string {
  return `${move.from}->${move.to}${move.capture ? ` 吃 ${move.capturedSquare}` : ""}`;
}

export function localizeErrorMessage(message: string): string {
  if (message.includes("Notation must contain a board and side")) {
    return "局面格式错误：请输入棋盘和走子方，例如 SSSSS/SSSSS/SSSSS/...../.CCC. c";
  }
  if (message.includes("Side must be c or s")) return "局面格式错误：走子方必须是 c 或 s。";
  if (message.includes("Board notation must contain five rows")) return "局面格式错误：棋盘必须包含 5 行。";
  if (message.includes("Each board row must contain five cells")) return "局面格式错误：每一行必须包含 5 个格子。";
  if (message.includes("Board cells must be C, S, or .")) return "局面格式错误：格子只能使用 C、S 或 .。";
  if (message.includes("exactly three cannons")) return "局面格式错误：必须正好有 3 个炮。";
  if (message.includes("Cannons and soldiers cannot overlap")) return "局面格式错误：炮和兵不能重叠。";
  if (message.includes("Select a tablebase directory or layer files first")) return "请先加载表库。";
  if (message.includes("No layer-XX.s15res files were selected")) return "未选择 layer-XX.s15res 分层文件。";
  if (message.includes("Missing layer-")) return message.replace("Missing", "缺少分层文件");
  if (message.includes("ruleset hash does not match")) return "规则集不匹配：该分层文件不属于当前规则。";
  if (message.includes("unsupported .s15res version")) return "不支持的 .s15res 版本。";
  if (message.includes("Unsupported .s15res encoding")) return "不支持的 .s15res 编码。";
  if (message.includes("invalid .s15res magic")) return "分层文件无效：文件头不匹配。";
  if (message.includes("payload size does not match")) return "分层文件无效：负载大小不匹配。";
  if (message.includes("file size does not match")) return "分层文件无效：文件大小不匹配。";
  if (message.includes("Directory picker is not available")) return "当前浏览器不支持目录选择，请改用分层文件选择。";
  if (message.includes("Tablebase selection was cancelled")) return "已取消表库选择。";
  if (message.includes("Max plies must be a positive integer")) return "最多手数必须是正整数。";
  return message;
}
