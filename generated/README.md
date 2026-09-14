# generated/

Submission artifacts for IEEE Transactions on Parallel and Distributed Systems.

Everything here is built from sources in this repository. Nothing here is edited
by hand, and nothing here is the source of truth: if a file disagrees with
`paper/`, the file here is stale and should be rebuilt.

| File | What it is |
|---|---|
| `vphilox-tpds.pdf` | the manuscript, 12 pages, uploaded as Main Document (PDF) |
| `vphilox-tpds-source.zip` | `vphilox.tex`, `IEEEtran.cls`, `figures/`, uploaded as Main Document (LaTeX) |
| `vphilox-cover-letter.pdf` | cover letter to the Editor-in-Chief |
| `vphilox-cover-letter.tex` | its source, which lives here because nothing else builds it |
| `submission-pack.md` | every ScholarOne form field, for copy and paste |

## Template

`paper/vphilox.tex` uses `\documentclass[lettersize,journal]{IEEEtran}`, which is
the sample file in IEEE's `Computer_Society_LaTeX_template.zip`. It is
deliberately not `[10pt,journal,compsoc]`. The how-to shipped in that same zip
still lists compsoc for Computer Society journals, but the editorial office
rejects compsoc submissions, so the sample file binds and the how-to does not.
Do not revert this on the strength of the how-to.

## Rebuilding

```bash
paper/build.sh --strict          # writes paper/vphilox.pdf, fails on an
                                 # overfull box or an undefined reference
cp paper/vphilox.pdf generated/vphilox-tpds.pdf
```

The zip is the flat directory arXiv and ScholarOne both want: `vphilox.tex`,
`IEEEtran.cls` from the TeX distribution, and the three staged figure PDFs.
`paper/stage-figures.sh` populates `paper/figures/`.

The cover letter:

```bash
cd generated
SOURCE_DATE_EPOCH=$(date -u -d 2026-09-14 +%s) FORCE_SOURCE_DATE=1 \
  pdflatex -interaction=nonstopmode -halt-on-error vphilox-cover-letter.tex
```

## The one check worth running

The zip and the PDF are uploaded as separate files, and an editor who rebuilds
the source expects the paper they already have. Unpack the zip somewhere clean,
build it, and compare:

```bash
cd "$(mktemp -d)" && unzip -q <path>/vphilox-tpds-source.zip && cd vphilox-source
export SOURCE_DATE_EPOCH=$(date -u -d 2026-09-14 +%s) FORCE_SOURCE_DATE=1
for i in 1 2; do pdflatex -interaction=nonstopmode -halt-on-error vphilox.tex; done
cmp vphilox.pdf <path>/vphilox-tpds.pdf
```

`paper/build.sh` pins `SOURCE_DATE_EPOCH` from `PAPER_DATE` for exactly this
reason, so the comparison is byte for byte rather than approximate. Bump
`PAPER_DATE` in `paper/build.sh` when the draft is revised and rebuild
everything here afterwards: the date appears on the title page.
