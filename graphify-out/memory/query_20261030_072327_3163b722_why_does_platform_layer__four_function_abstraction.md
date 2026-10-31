---
type: "query"
date: "2026-10-30T07:23:27.804284+00:00"
question: "Why does Platform layer (four-function abstraction) connect Documented Platform Layer to Documented Design Concepts?"
contributor: "graphify"
outcome: "useful"
source_nodes: ["Platform layer (four-function abstraction)", "Single-file, zero-dependency design", "term_key", "Windows terminal backend", "POSIX terminal backend"]
---

# Q: Why does Platform layer (four-function abstraction) connect Documented Platform Layer to Documented Design Concepts?

## Answer

Expanded from original query via vocab: [platform, layer, four, function, abstraction, design, single, dependency, zero, term, enter, leave]. Then traversed BFS depth 2 from readme_platform_layer. Of the bridge node's 7 edges, exactly 2 cross the community boundary into Documented Design Concepts: (1) Single-file zero-dependency design --rationale_for--> Platform layer [INFERRED 0.85, README.md L3 and L82-L86], the design constraint that forces the four-function seam to exist at all; and (2) Platform layer --references--> term_key [EXTRACTED 1.0, README.md L83], where term_key is the only one of the four functions clustered into Design Concepts because it also carries user-facing behaviour (timed ESC read, and an INFERRED link from the ACCESS GRANTED/DENIED placard at L38-L44). The other five edges stay inside Documented Platform Layer: Windows and POSIX backends implement it, and it references term_enter, term_leave, term_size. Note the node has file_type rationale, not code: no equivalent node exists on the AST side, and in hackertyper.c the four functions scatter across three code communities, so the abstraction is documentation-only and has no structural existence the AST can see.

## Outcome

- Signal: useful

## Source Nodes

- Platform layer (four-function abstraction)
- Single-file, zero-dependency design
- term_key
- Windows terminal backend
- POSIX terminal backend