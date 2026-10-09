# generated/

Submission artifacts for ACM Transactions on Mathematical Software.

Everything here is built from sources in this repository. Nothing here is edited
by hand, and nothing here is the source of truth: if a file disagrees with
`paper/`, the file here is stale and should be rebuilt.

TOMS reviews new submissions double-anonymous, so the manuscript exists twice.
Only the anonymous one goes to the portal.

| File | What it is |
|---|---|
| `vphilox-toms-anonymous.pdf` | 19 pages, the main document for review |
| `vphilox-toms-title-page.pdf` | title, author, affiliation, the conflict disclosure, and the two identifiers the anonymous build withholds |
| `vphilox-toms-title-page.tex` | its source |
| `vphilox-cover-letter.pdf` | cover letter to the Editor-in-Chief |
| `vphilox-cover-letter.tex` | its source, which lives here because nothing else builds it |
| `vphilox-toms.pdf` | 20 pages, the named build. The repository's and arXiv's copy, not the portal's |
| `vphilox-toms-source.zip` | the LaTeX source. For after acceptance: it names the author |
| `submission-pack.md` | every submission form field, for copy and paste |

The previous bundle was for IEEE TPDS, which desk-rejected the paper on scope in
October 2026 without sending it to referees. That history is in
`docs/publishing-guide.md`; the files it produced are in git and are not here.

## Template

`paper/vphilox.tex` uses `\documentclass[acmsmall,screen,review]{acmart}`, from
the template at <https://www.acm.org/publications/proceedings-template>.
`acmsmall` is the ACM small-format journal layout that TOMS uses. `review`
numbers the lines for referees. `anonymous` is absent on purpose: TOMS makes the
referees anonymous to the author rather than the author anonymous to the
referees, which is what lets Section 1 disclose that this paper's author
submitted the pull request it quotes.

`acmart.cls` and `ACM-Reference-Format.bst` are vendored under `paper/acmart/`
at v2.20 rather than taken from the TeX tree, which on TeX Live 2025 is v2.16.
Both are LPPL and `paper/acmart/LICENSE` travels with them.

The fonts acmart calls for are not in a base TeX install. Libertine, newtx and
inconsolata are all in `texlive-fonts-extra`. Without them acmart falls back to
bitmap Computer Modern and microtype stops the run with *auto expansion is only
possible with scalable fonts*, which is the first thing to check if the build
dies with no PDF and no obvious error.

## Rebuilding

```bash
paper/stage-figures.sh                    # populates paper/figures/
paper/build.sh --strict                   # -> paper/vphilox.pdf, the named one
paper/build.sh --anonymous --strict       # -> paper/vphilox-anon.pdf
cp paper/vphilox.pdf      generated/vphilox-toms.pdf
cp paper/vphilox-anon.pdf generated/vphilox-toms-anonymous.pdf
```

Both come from one source. `--anonymous` passes the class option through
`\PassOptionsToClass` and builds under a separate jobname, so neither
overwrites the other, and three `\ifanon` branches in the body cover what the
class cannot see. Adding anonymous mode did not move the named PDF's bytes.

The zip is the flat directory both ACM and arXiv want. It carries the class and
the bibliography style alongside the source, so it builds on a machine that has
neither, and it carries `vphilox.bbl` so a submission system that does not run
BibTeX still gets the references.

The cover letter:

```bash
cd generated
SOURCE_DATE_EPOCH=$(date -u -d 2026-10-09 +%s) FORCE_SOURCE_DATE=1 \
  pdflatex -interaction=nonstopmode -halt-on-error vphilox-cover-letter.tex
```

## The one check worth running

The zip and the PDF are uploaded as separate files, and an editor who rebuilds
the source expects the paper they already have. Unpack the zip somewhere clean,
build it, and compare:

```bash
cd "$(mktemp -d)" && unzip -q <path>/vphilox-toms-source.zip && cd vphilox-source
export SOURCE_DATE_EPOCH=$(date -u -d 2026-10-09 +%s) FORCE_SOURCE_DATE=1
pdflatex -interaction=nonstopmode -halt-on-error vphilox.tex
bibtex vphilox
for i in 1 2; do pdflatex -interaction=nonstopmode -halt-on-error vphilox.tex; done
cmp vphilox.pdf <path>/vphilox-toms.pdf
```

`paper/build.sh` pins `SOURCE_DATE_EPOCH` from `PAPER_DATE` for exactly this
reason, so the comparison is byte for byte rather than approximate. Bump
`PAPER_DATE` in `paper/build.sh` when the draft is revised and rebuild
everything here afterwards: the date appears on the title page.

One thing had to change for that comparison to hold. Both `\graphicspath`
entries hold the same figures, and pdftex stamps whichever path it loaded each
one from into the output as `/PTEX.FileName`, so the same source produced
different bytes depending on whether `stage-figures.sh` had been run.
`\pdfsuppressptexinfo=-1` in the preamble drops those stamps, and the build is
now path-independent.
