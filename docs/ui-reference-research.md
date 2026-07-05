# UI Reference Research

Task: `wdl-line-explorer-and-ui-foundation`

This pass reviewed board-game and chess-family UI projects for interaction and
architecture ideas. The implementation in this repository does not copy their
code or art assets.

## Summary

The useful pattern is to keep the board component UI-only: render pieces,
selection, legal destinations, last move, and annotations, while rules,
history, and tablebase lookup live in separate modules. This matches Sanpao15's
current direction: `engine.ts` owns rules/notation, `board.ts` renders the
board, and `tablebase/*` reads `.s15res` outcomes and builds WDL guidance.

The web UI should stay on Vite for now. The browser File System Access API and
file-input fallback are enough for read-only local `.s15res` access, and the UI
can random-read slices without loading the full outcome table. Electron or
Tauri may be useful later for a packaged local tablebase manager, but this task
did not find a reason to migrate immediately. Native Qt/SDL would give direct
file access but would raise development cost and slow iteration.

## Projects Reviewed

| Project | URL | License | Relevant ideas | Reuse policy |
| --- | --- | --- | --- | --- |
| chessground | https://github.com/lichess-org/chessground | GPL-3.0 | Board UI separated from chess rules, legal destination highlights, last-move highlights, movable/droppable state, annotations/arrows. | Learn concepts only. Do not copy code or assets into this project unless the whole licensing strategy changes. |
| chessboard.js | https://github.com/oakmac/chessboardjs | MIT | Board widget API, position object, drag/drop callbacks, spare-piece ideas, UI as a component rather than an engine. | MIT is permissive, but we only used architectural ideas. No code copied. |
| react-chessboard | https://github.com/Clariity/react-chessboard | MIT | React component pattern, custom pieces, arrows, premove-style overlays, responsive board sizing. | Concepts are safe; project is React while Sanpao15 currently uses plain TypeScript/Vite. No code copied. |
| boardgame.io | https://github.com/boardgameio/boardgame.io | MIT | Strong separation of game state, moves, phases/turns, and UI clients. | Useful design reference for module boundaries. No code copied. |
| XBoard / WinBoard | https://www.gnu.org/software/xboard/ | GPL | Mature chess-variant UI, move list, engine integration, variant support, board/piece themes. | GPL project: learn high-level product ideas only. Do not copy code or bundled piece themes. |
| Fairy-Max | https://www.gnu.org/software/xboard/ | GPL-family chess-variant engine ecosystem | Variant handling and compact rule representation are relevant as engine ideas, not UI assets. | Learn concepts only; do not copy engine code. |
| VASSAL Engine | https://vassalengine.org/ | LGPL/GPL ecosystem | General tabletop module model, local assets, logs, playback, and board annotations. | Use only conceptual inspiration for replay/playback and module separation. Verify any asset license before use. |
| Xiangqi board projects | GitHub search results vary by project | Mixed: MIT, GPL, or unclear | Chinese-chess board layouts, piece glyphs, mobile interactions. | Per-project license must be verified before reuse. Unclear-license piece images should not be used. |
| chessboard-element / cm-chessboard | GitHub/web component chessboard projects | Generally permissive for some projects, verify per version | Web component packaging, SVG/piece customization, responsive sizing. | Useful reference for component surfaces. No code or assets copied. |

## Interaction Ideas Adopted

- Board rendering is UI-only and receives state plus overlays.
- Legal moves are highlighted after selecting a piece.
- Last move and recommended moves are separate overlays.
- History is explicit: undo, redo, and reset operate on position snapshots.
- Position notation is copyable/pasteable and round-trips through one parser.
- WDL line explorer is a sample line player, not a distance-optimized proof.
- Playback uses a route list plus previous/next/autoplay controls.

## Asset Notes

No third-party image, SVG, font, or piece set was imported. Current piece SVGs
are self-created local placeholders under `ui/src/assets/pieces/`. Future asset
replacement should use only clearly licensed assets, preserve attribution, and
document license terms in `docs/assets.md`. Web images must not be hotlinked.

## License Guardrails

- GPL projects can inform architecture and UX but must not be copied into this
  repository unless the repository license strategy is changed deliberately.
- Unclear-license Xiangqi/chess piece images are not acceptable.
- MIT-style projects are compatible for learning and possibly reuse, but this
  task still avoided code copying to keep provenance simple.
- UI text must not claim shortest win, fastest draw, DTW, or DTC while `.s15res`
  stores only WDL outcomes.
