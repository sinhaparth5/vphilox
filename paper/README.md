# The paper

Phase 5 draft of *Portable, Seekable Random Streams for Parallel CPU
Simulation*, targeting an arXiv preprint under cs.PF / cs.MS.

`vphilox.tex` is a single standalone file with an inline `thebibliography`.
That is deliberate: arXiv takes a flat directory happily, and one file plus
five figure PDFs is the whole submission. Switching to an external `.bib`
later is a preamble change plus `\bibliography{refs}`.

## Build

```bash
./build.sh                  # -> vphilox.pdf
./build.sh --strict         # the same, but exit non-zero on overfull boxes
                            # or undefined references. This is what CI runs.
```

CI's `paper builds` job compiles the paper whenever anything under `paper/` or
`docs/benchmarks/plots/` changes, so the source is checked even by contributors
with no TeX install, and it uploads the resulting PDF as a run artifact named
`vphilox-paper`. **It cannot check the tracked PDF the way `--check` checks the
figures**, because two TeX distributions do not produce identical bytes; instead
it warns when `vphilox.tex` was committed more recently than `vphilox.pdf`,
which is exactly the staleness that matters. Download the artifact if you want
the current PDF without installing TeX.

`vphilox.pdf` is **tracked**, so the paper can be read without a TeX install.
That only works if a rebuild with unchanged sources produces an unchanged file,
so `build.sh` pins `SOURCE_DATE_EPOCH`: without it, pdftex stamps the wall
clock into the PDF and `\today` moves the title-page date, and every rebuild
churns 300 KB of binary diff for no content change. Bump `PAPER_DATE` in
`build.sh` when the draft is revised.

Plain `pdflatex vphilox.tex` twice works too and produces the same pages; it
just is not byte-reproducible, so prefer `build.sh` for anything you commit. The figures are **not** duplicated into this
directory: `\graphicspath` falls back to `../docs/benchmarks/plots/`, which is
tracked, so a fresh clone compiles all four figures with no staging step.

`./stage-figures.sh` copies them into `figures/` and is needed only for arXiv,
which wants a flat directory. It runs `publish_results.py --check` first, so a
stale figure fails loudly instead of ending up in the submission. `figures/` is
gitignored because those copies are generated; the tracked originals live under
`docs/`.

Builds clean at **12 pages**: zero errors, zero overfull boxes, zero undefined
references, verified by CI rather than asserted. The count was 15 under the
`article` class this draft started in; two-column IEEEtran is denser, so the
paper lost three pages while gaining the Phase 4 sections.

The tracked `vphilox.pdf` is the one CI built, so it is current with the source.

## Where the numbers come from

Every figure and every table maps onto an archived run. Nothing in the draft
was typed from memory.

Sources are CSVs and raw JSON rather than prose. The per-run write-ups that
used to sit in `docs/benchmarks/` were removed once their conclusions had been
folded into `docs/benchmarks/README.md`, the paper itself and `CLAUDE.md`; the
measurements they described are all still in `raw/` and `results/`, and the
pages themselves are in git history.

Rows are keyed by `\label` rather than by number, because the numbers move
whenever a table is added.

| Paper | Source |
|---|---|
| `tab:machines` | `docs/benchmarks/raw/machines.csv`, except the MHz column — see below |
| `tab:matrix` (relative matrix) | `docs/benchmarks/raw/*-matrix.csv`, five hosts |
| `tab:avx512` (AVX-512 vs AVX2) | `results/{sapphire-rapids,skylake-sp}-matrix.json`, pinned-backend runs |
| `tab:icache` (instruction supply) | `docs/benchmarks/raw/tigerlake-icache-{phys,ht}-icache.csv` |
| `tab:runtimes` (threading runtimes) | `docs/benchmarks/raw/tigerlake-omp-l1-{default,passive}-scaling.csv` |
| `fig:matrix`, `fig:sweep` | `plots/matrix-relative`, `generate-n-sweep` |
| `sec:float` (conversion widths) | `docs/benchmarks/raw/*float-conversion*` — quoted inline; the figure itself is published under `docs/` but is not in the manuscript |
| `fig:scaling` (thread scaling) | `docs/benchmarks/raw/pi-arm-scaling.csv` |
| `fig:placement` | `docs/benchmarks/raw/cascadelake-32v-placement-*.csv` |
| `sec:neon` (NEON unroll) | `docs/benchmarks/raw/pi5-matrix.csv`, `pi-arm-matrix.csv` |
| `sec:placement` | `docs/benchmarks/raw/cascadelake-32v-placement-*.csv` |
| `sec:stat` (statistics) | `docs/statistical-validation.md`, `results/practrand/`, `results/testu01/` |
| `sec:curand` | `docs/curand-parity.md`, `results/curand/` |

The one column not taken directly from a CSV field is `tab:machines`'s MHz,
which carries each host's nominal clock. `machines.csv` has a `reported_mhz`
column, but that is Google Benchmark's reading of the clock at run start rather
than a machine specification, and on an idle laptop it reads an order of
magnitude low. The nominal figures come from the same file's `cpu` column
instead, where the x86 model strings state the base clock outright
(`Xeon(R) Platinum 8481C CPU @ 2.70GHz`). The Pi is the exception, because
`Cortex-A76` names no clock: its 2400 MHz comes from `CPU max MHz` in
`results/pi-arm-baseline-environment.txt`.

The two new tables are tables and not figures on purpose. Both are two-arm
contrasts of four numbers, which a plot makes harder to read rather than easier,
and adding them to `publish_results.py` would put two more byte-reproducible
SVG/PDF pairs under CI's `--check` for no gain in legibility.

## Where it goes when it is done

[`docs/publishing-guide.md`](../docs/publishing-guide.md) has the route. The
short version: Zenodo archives the software and its data under one record and
the manuscript under a second, related one, and arXiv comes last because
submitting there requires an endorsement. TechRxiv was the intended host for the
manuscript and closed its submissions in August 2026. The two Zenodo records
must be linked by a relation (`IsDocumentedBy` / `IsSupplementTo`); what to avoid
is two competing DOIs for the *same* artifact, not a software record and a paper
record.

arXiv wants the LaTeX source rather than a locally built PDF, which is what
`./stage-figures.sh` is for: run it, then submit `vphilox.tex` plus
`figures/*.pdf` as a flat directory.

## What is still open

The `\todo` markers in the source are the list, and there is one left: the
**Zenodo DOI**, in the Availability section and in `CITATION.cff`. It cannot be
filled before the release exists, so it is not a writing task.

The affiliation and ORCID are filled: *Independent researcher, Oxford, United
Kingdom*, ORCID `0009-0002-3120-9301`, in both the author block and
`CITATION.cff`. Nothing red is left on the title page.

The XGBoost citation, which used to be the item blocking publication, is filled.
Section 1 quotes the review comment on
[dmlc/xgboost#12485](https://github.com/dmlc/xgboost/pull/12485#issuecomment-5355357794)
verbatim rather than paraphrasing it, and a footnote discloses that the author
of this paper submitted the proposal that comment declined. The section also now
cites [dmlc/xgboost#12459](https://github.com/dmlc/xgboost/issues/12459), an
unrelated user's report of the same portability failure in the field, which is
what turns the opening from an argument from the standard library sources into
an argument with a witness.

One thing on the list is not a repository task at all. The guide asks for at
least one knowledgeable reader to go over the claims, citations, methodology,
figures and language before the preprint is posted. Nothing in CI substitutes for
that.

Three earlier entries are closed. The PractRand release is pinned to
`PractRand-pre0.95` with the SourceForge project URL; the cuRAND reference is
pinned to cuRAND 10.4.1 from CUDA Toolkit 13.1, which is what
`results/curand/curand-parity-tigerlake.txt` records; and the two measurements
Section 10 previously listed as pending hardware have both been made and are now
Sections 8.3, 8.4 and 9.1.

## Template

This draft uses `IEEEtran` in the Computer Society journal format, which is what
TPDS, TC and TSE use. A conference submission changes the class options to
`[conference]` and drops the `\IEEEcompsoc*` commands in the author block;
nothing in the body depends on the class. `texlive-publishers` provides
`IEEEtran`.

## House rules the prose follows

No em dashes, no contractions, no rhetorical questions, no "notably" or
"importantly", "we" rather than "I". Results that cut against the library stay
in: Section 10 is where xoshiro256++ leading on four of five machines and the
ARM buffered engine trailing `std::mt19937` are stated, and neither is softened.
