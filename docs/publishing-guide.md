# Publishing the vphilox Paper

This guide records the recommended route for making the paper and its supporting
software public as an independent researcher. Each service has a different job:

- **Zenodo** preserves the software, benchmark data, and reproducibility files,
  and — since TechRxiv closed its submissions — hosts the paper as well, under a
  second, separate record.
- **arXiv** provides the eventual subject-indexed preprint after endorsement.
- A journal or conference provides peer review; none of the services above does.

## 1. Archive the Software on Zenodo

Create a stable GitHub release of vphilox, then archive that release on Zenodo.
Reserve the DOI before publishing so it can be added to both `CITATION.cff` and
the paper's Availability section.

The Zenodo record should describe the **software**, not the manuscript. Include
the tagged source, benchmark data, scripts, documentation, version number,
license, repository URL, and ORCID. Once checked, publish the record to activate
the DOI. See the [Zenodo DOI guide](https://help.zenodo.org/docs/deposit/describe-records/reserve-doi/).

**Done.** The reserved DOI is
[10.5281/zenodo.22103483](https://doi.org/10.5281/zenodo.22103483), and it is
recorded in `CITATION.cff`, `README.md`, `CHANGELOG.md` and the paper's
Availability section. Zenodo also mints a concept DOI covering every version of
a record; if this one has one, `CITATION.cff` should carry that instead, so a
citation survives the next release.

## 2. Finish the Manuscript

Before publishing the preprint:

- ~~Use `Independent Researcher, City, Country` as the affiliation.~~ Done:
  Oxford, United Kingdom, with the ORCID beside it.
- ~~Replace every visible TODO and placeholder.~~ Done: no `\todo` and no
  `\textcolor` placeholder remains, and CI's `release readiness` job fails a tag
  that reintroduces either.
- ~~Add the source for the previously reported tenfold slowdown.~~ Done: §1
  quotes dmlc/xgboost#12485 verbatim, with a footnote disclosing that this
  author submitted the proposal it declined.
- ~~Record the PractRand release and the CUDA/cuRAND version.~~ Done:
  PractRand-pre0.95 and cuRAND 10.4.1 / CUDA Toolkit 13.1, both in the
  bibliography.
- ~~Add the Zenodo software DOI and rebuild `paper/vphilox.pdf`.~~ Done.
- **Open.** Ask at least one knowledgeable reader to check the claims,
  citations, methodology, figures, and language. It is the one item nothing in
  the repository can substitute for, and step 4 explains why it is worth doing
  before the preprint rather than after: the reader and the arXiv endorser can
  be the same person.

## 3. Post the Paper as a Preprint

The original route here was TechRxiv, and it is no longer available: as of
August 2026 the site carries an update notice saying it is transitioning to a
new platform, that **submissions are temporarily closed**, and that existing
content and DOIs continue to resolve. No reopening date is published. Treat it
as unavailable rather than gone, and do not wait for it.

This step was never load-bearing. Its one job was to give the manuscript a
citable DOI to put in front of a prospective arXiv endorser, and that job is
host-agnostic — arXiv endorsement does not require a preprint at all, since
endorsers judge the manuscript itself.

Use a **second Zenodo record, describing the paper**, and set its relation to
the software record (`IsDocumentedBy`, or `IsSupplementTo` from the software
side) so the two read as a linked pair:

```text
Zenodo DOI #1 -> software and reproducibility archive
Zenodo DOI #2 -> the paper, related to #1
arXiv ID      -> later arXiv version
```

That is what the earlier "do not also upload the manuscript to Zenodo" warning
was guarding against — two competing DOIs for *one* artifact. A software record
plus a paper record is the split this guide already recommends; the relation
field is what keeps them from competing.

The alternatives, if a moderated third-party archive is wanted instead: HAL
(free, permanent, open to independent researchers, no endorsement needed), SSRN,
or Preprints.org. All three are slower, because all three have a moderation
queue. None of them blocks a later arXiv submission — arXiv accepts work posted
elsewhere as long as the author holds copyright.

## 4. Find an arXiv Endorser

Create an arXiv account, start a submission with **cs.PF (Performance)** as the
primary category, and wait for arXiv to generate an endorsement request. Use
**cs.MS (Mathematical Software)** as a suitable secondary category.

Search recent related papers in cs.PF, cs.MS, or cs.DC. On an abstract page,
select **Which authors of this paper are endorsers?** Contact one relevant
researcher at a time and provide:

- The paper's Zenodo DOI, or the PDF itself.
- The public GitHub repository and Zenodo software DOI.
- Your ORCID or professional profile.
- The arXiv endorsement link or code.
- A short explanation of how their research relates to the paper.

Do not pay for endorsement, trade endorsements, or send mass requests. The
[official endorsement guide](https://info.arxiv.org/help/endorsement.html)
explains the current rules. Contact one person, then wait about a week before
moving to the next.

Note that arXiv's *automatic* endorsement keys off registering with a recognised
institutional email address. It will not fire for an independent researcher on a
personal address, so endorsement here is a real gate rather than a formality.

**This search and the open item in step 2 are the same search.** A researcher
who publishes on PRNG design, SIMD kernels, or reproducible simulation is both a
qualified endorser and the knowledgeable reader the manuscript still needs. Ask
such a person to *read* the paper rather than only to endorse it: someone who
has actually engaged with the work writes a far better endorsement, and may
return a correction worth having. Run the search once, for both purposes.

## 5. Submit to arXiv

After endorsement, upload the LaTeX source and figure PDFs rather than only the
locally generated PDF. Run `paper/stage-figures.sh`, package `vphilox.tex` and
`figures/*.pdf`, then inspect arXiv's generated PDF carefully before completing
the submission. Follow the [official submission guide](https://info.arxiv.org/help/submit/index.html).

## 6. Submit for Peer Review

Preprints are not the end state; none of the services above provides review.
The venue is **IEEE Transactions on Parallel and Distributed Systems (TPDS)**,
chosen on cost. Submit through
[ScholarOne](https://mc.manuscriptcentral.com/tpds-cs) as a **Regular Paper**;
TPDS does not accept survey or comment-style submissions.

### Why TPDS and not TOMACS

ACM TOMACS is the better fit on scope — its charter names "random number
generators and testing" outright — and it runs a Replicated Computational
Results initiative whose badges this repository is unusually well prepared to
earn, since `run_matrix.sh` records provenance and gates on CV,
`publish_results.py --check` regenerates every figure in CI, and every
PractRand and TestU01 log carries a git SHA.

It was ruled out on price. ACM became a fully open-access publisher on
1 January 2026 and retired the subscription track, so an accepted TOMACS paper
carries a mandatory APC. The 2026 subsidised rate is reported as $250 for
ACM/SIG members and $350 otherwise, but that subsidy is announced in terms of
conferences and a separate source quotes $1,450 for journals, so the real
number is unconfirmed. ACM's discretionary waiver policy states explicitly that
being an independent researcher without an institutional affiliation is not by
itself a demonstration of financial hardship, so no waiver is available here.

TPDS is hybrid: the traditional, non-open-access track carries no APC. Author-
paid open access exists as an option and is not needed. The paper is also
already written in IEEEtran, so no conversion is required.

The cost of the free route is time rather than money. TPDS scope is parallel and
distributed systems, and a desk rejection on fit would cost a month or two and
force the venue choice again.

### Two constraints before submitting

- **Twelve pages.** TPDS caps review versions at 12 pages, and
  `paper/vphilox.pdf` is exactly 12. Overlength charges on the accepted version
  are mandatory and non-negotiable, so a reviewer asking for one more experiment
  is what turns a free submission into a paid one. Trim to 11 first.
- **Lead with the parallel result.** As drafted, the paper's spine is portable
  serialized state and single-threaded throughput, with the parallel material in
  section 8. For TPDS the contribution has to be the parallel-systems claim:
  streams that are bit-identical regardless of thread count and scheduling,
  O(1) seek, and checkpoints that survive moving between machines in a
  heterogeneous cluster. The hyperthreading result — the multi-core knee is
  execution-port contention rather than the memory system, with frequency
  excluded by direct measurement rather than argument — is the most TPDS-shaped
  finding in the paper and should not sit two thirds of the way in.

IEEE permits preprints on arXiv and other repositories; after acceptance the
posted version must carry the IEEE copyright notice. Posting to Zenodo first
does not compromise the submission, and a manuscript under review is a stronger
thing to tell a prospective arXiv endorser than a preprint link alone.
