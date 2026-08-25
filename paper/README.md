# The paper

Phase 5 draft of *Portable, Seekable Random Streams for Parallel CPU
Simulation*, targeting an arXiv preprint under cs.PF / cs.MS.

`vphilox.tex` is a single standalone file with an inline `thebibliography`.
That is deliberate: arXiv takes a flat directory happily, and one file plus
five figure PDFs is the whole submission. Switching to an external `.bib`
later is a preamble change plus `\bibliography{refs}`.

## Build

```bash
./stage-figures.sh          # copies the published figure PDFs into figures/
pdflatex vphilox.tex
pdflatex vphilox.tex        # second pass resolves \cref and the TOC
```

**This draft has not been compiled.** No TeX distribution is installed on the
machine it was written on, so the checks that were run are structural rather
than a build: environment balance, every `\ref`/`\cref` resolving to a label,
every `\cite` resolving to a `\bibitem`, no unused macros, balanced braces, and
all five referenced figure files present. Expect the first real `pdflatex` run
to surface something these cannot see. Install with
`sudo apt install texlive-latex-recommended texlive-latex-extra texlive-science`.

The figures come from `docs/benchmarks/plots/`, which is **generated** by
`scripts/benchmarks/publish_results.py` from the JSON in `results/`.
`stage-figures.sh` runs that script in `--check` mode before copying, so a
stale figure fails loudly instead of ending up in the paper. `figures/` is
gitignored for the same reason: the checked-in copy lives under `docs/`.

## Where the numbers come from

Every figure and every table maps onto an archived run. Nothing in the draft
was typed from memory.

| Paper | Source |
|---|---|
| Table 2 (relative matrix) | `docs/benchmarks/raw/*-matrix.csv`, five hosts |
| Table 3 (AVX-512 vs AVX2) | `docs/benchmarks/avx512-downclocking-2026-08-24.md` |
| Figures 1, 2, 5 | `plots/matrix-relative`, `generate-n-sweep`, `float-conversion-widths` |
| Figure 3 (thread scaling) | `docs/benchmarks/raw/pi-arm-scaling.csv` |
| Figure 4 (placement) | `docs/benchmarks/raw/cascadelake-32v-placement-*.csv` |
| Section 9 (statistics) | `docs/statistical-validation.md`, `results/practrand/`, `results/testu01/` |
| Section 4.3 (NEON unroll) | `docs/benchmarks/neon-unroll-pi-2026-08-25.md` |
| Section 8.2 (placement) | `docs/benchmarks/scaling-cascade-lake-2026-08-25.md` |

## What is still open

The `\todo` markers in the source are the list, and there are six:

1. **Affiliation line** on the title page.
2. **The XGBoost citation.** The paper quotes a tenfold-slowdown figure from an
   upstream discussion and answers it with measurements. The thread URL and an
   access date are needed before that section can go out; paraphrasing the
   quoted number without the source is not acceptable in a paper whose main
   evaluation exists to rebut it.
3. **PractRand release** and its URL.
4. **cuRAND toolkit version**, once the GPU parity reference (#54) is measured.
5. **Zenodo DOI**, in the Availability section and in `CITATION.cff`.

Two measurements named as pending in Section 10 are genuinely pending, not
missing text: the instruction-cache placement study (#53) needs a bare-metal
multi-core host, and the cuRAND parity reference (#54) needs an NVIDIA device.
Neither is claimed as a result.

## Template

The roadmap names the ACM template. This draft uses `article` with the standard
preamble instead, because `acmart` is not installed here and an uncompilable
class swap is worse than a compilable one that is easy to change later. Nothing
in the body depends on `article`: the swap is the preamble, the title block and
the bibliography style.

## House rules the prose follows

No em dashes, no contractions, no rhetorical questions, no "notably" or
"importantly", "we" rather than "I". Results that cut against the library stay
in: Section 10 is where xoshiro256++ leading on four of five machines and the
ARM buffered engine trailing `std::mt19937` are stated, and neither is softened.
