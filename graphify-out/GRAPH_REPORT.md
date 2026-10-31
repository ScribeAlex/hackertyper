# Graph Report - scratch-2026-10-30-6ec391  (2026-10-30)

## Corpus Check
- 1 files · ~4,640 words
- Verdict: corpus is large enough that graph structure adds value.

## Summary
- 76 nodes · 164 edges · 7 communities (6 shown, 1 thin omitted)
- Extraction: 88% EXTRACTED · 12% INFERRED · 0% AMBIGUOUS · INFERRED: 20 edges (avg confidence: 0.88)
- Token cost: 93,990 input · 0 output

## Community Hubs (Navigation)
- Documented Render And Options
- Headers And Teardown
- Platform Contract And Graph Memory
- Frame Buffer And Rendering
- Startup And Input Loop
- Screen Grid And Scrolling
- Syntax Lexer

## God Nodes (most connected - your core abstractions)
1. `main()` - 14 edges
2. `render()` - 11 edges
3. `Four-function contract block (single declaration above the #ifdef)` - 10 edges
4. `Platform layer (four-function abstraction)` - 9 edges
5. `Single-fwrite frame composition (Buf)` - 8 edges
6. `draw_overlay()` - 7 edges
7. `screen_putc()` - 7 edges
8. `Built-in payload (fictional fabric/quantum_lane.c)` - 7 edges
9. `buf_put()` - 6 edges
10. `Keyboard as a crank (keystrokes reveal, never interpret)` - 6 edges

## Surprising Connections (you probably didn't know these)
- `Documented Design Concepts (graph community)` --references--> `ACCESS GRANTED / ACCESS DENIED placard`  [INFERRED]
  graphify-out/memory/query_20261030_072327_3163b722_why_does_platform_layer__four_function_abstraction.md → README.md
- `Cross-community bridge node (documentation-only abstraction)` --conceptually_related_to--> `Four-function contract block (single declaration above the #ifdef)`  [INFERRED]
  graphify-out/memory/query_20261030_072327_3163b722_why_does_platform_layer__four_function_abstraction.md → README.md
- `Cross-community bridge node (documentation-only abstraction)` --references--> `Platform layer (four-function abstraction)`  [EXTRACTED]
  graphify-out/memory/query_20261030_072327_3163b722_why_does_platform_layer__four_function_abstraction.md → README.md
- `Documented Design Concepts (graph community)` --references--> `term_key`  [EXTRACTED]
  graphify-out/memory/query_20261030_072327_3163b722_why_does_platform_layer__four_function_abstraction.md → README.md
- `Documented Design Concepts (graph community)` --references--> `Single-file, zero-dependency design`  [EXTRACTED]
  graphify-out/memory/query_20261030_072327_3163b722_why_does_platform_layer__four_function_abstraction.md → README.md

## Import Cycles
- None detected.

## Hyperedges (group relationships)
- **Platform layer as the bridge between the two documentation communities** — graphify_out_memory_query_20261030_072327_3163b722_why_does_platform_layer__four_function_abstraction_community_bridge_node, graphify_out_memory_query_20261030_072327_3163b722_why_does_platform_layer__four_function_abstraction_documented_platform_layer, graphify_out_memory_query_20261030_072327_3163b722_why_does_platform_layer__four_function_abstraction_documented_design_concepts, readme_platform_layer, readme_term_key, readme_zero_dependency_single_file [EXTRACTED 1.00]
- **Four-function platform contract, declared once and defined twice** — readme_contract_block, readme_platform_layer, readme_term_enter, readme_term_leave, readme_term_size, readme_term_key, readme_windows_backend, readme_posix_backend [EXTRACTED 1.00]
- **Flicker-free render pipeline (lex once, fill grid, compose one frame)** — readme_paint, readme_tone_array, readme_screen_putc, readme_cell_grid, readme_frame_buf, readme_colour_run_coalescing, readme_tone_ansi, readme_status_bar, readme_placard [EXTRACTED 1.00]
- **Option surface: each option reaches exactly one place in the program** — readme_option_file, readme_option_speed, readme_option_mono, readme_option_help [EXTRACTED 1.00]

## Communities (7 total, 1 thin omitted)

### Community 0 - "Documented Render And Options"
Cohesion: 0.24
Nodes (15): Cell grid (screen model), Single-fwrite frame composition (Buf), hackertyper, hackertyper.net (original site), --file PATH option, --mono option, --speed N option, paint (startup lexer) (+7 more)

### Community 2 - "Platform Contract And Graph Memory"
Cohesion: 0.34
Nodes (12): Cross-community bridge node (documentation-only abstraction), Documented Design Concepts (graph community), Documented Platform Layer (graph community), --help option, Platform layer (four-function abstraction), POSIX terminal backend, restore_terminal (idempotent, atexit), term_enter (+4 more)

### Community 3 - "Frame Buffer And Rendering"
Cohesion: 0.36
Nodes (9): buf_need(), buf_printf(), buf_put(), buf_putc(), buf_puts(), buf_reset(), draw_overlay(), render() (+1 more)

### Community 4 - "Startup And Input Loop"
Cohesion: 0.29
Nodes (7): main(), payload_builtin(), slurp(), term_enter(), term_key(), term_size(), usage()

### Community 5 - "Screen Grid And Scrolling"
Cohesion: 0.43
Nodes (6): screen_alloc(), screen_free(), screen_newline(), screen_print(), screen_putc(), screen_scroll()

### Community 6 - "Syntax Lexer"
Cohesion: 0.50
Nodes (4): at_line_start(), in_table(), is_word_byte(), paint()

## Knowledge Gaps
- **1 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `Keyboard as a crank (keystrokes reveal, never interpret)` connect `Documented Render And Options` to `Platform Contract And Graph Memory`?**
  _High betweenness centrality (0.031) - this node is a cross-community bridge._
- **Are the 3 inferred relationships involving `Four-function contract block (single declaration above the #ifdef)` (e.g. with `Cross-community bridge node (documentation-only abstraction)` and `restore_terminal (idempotent, atexit)`) actually correct?**
  _`Four-function contract block (single declaration above the #ifdef)` has 3 INFERRED edges - model-reasoned connections that need verification._