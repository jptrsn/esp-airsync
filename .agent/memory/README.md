# Project memory

Durable, topic-organized learnings for this repo (AI-DLC "files are memory"
convention). The repo is the memory: read the relevant topic file before
starting related work, and add distilled learnings after.

Rules
- Organize by **topic**, not chronology. One file per topic.
- Keep entries concise and reusable: what was learned, why it matters, how to
  apply it next time.
- Record bug resolutions with: symptom, root cause, evidence, fix, and the
  reusable learning (so the same class of bug is found faster next time).
- **Commit these files.** Uncommitted memory is lost memory.

Topics
- [`raop-alac-decode-stack-overflow.md`](./raop-alac-decode-stack-overflow.md)
  — intermittent mid-track audio glitch; root cause was a 32-bit store into a
  16-bit stack var in the ALAC decode path (fork regression vs squeezelite).
