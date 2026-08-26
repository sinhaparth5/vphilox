# Publishing the vphilox Paper

This guide records the recommended route for making the paper and its supporting
software public as an independent researcher. Each service has a different job:

- **Zenodo** preserves the software, benchmark data, and reproducibility files.
- **TechRxiv** hosted the paper as a computer-science preprint, and is closed
  to new submissions during a platform migration (see §3).
- **arXiv** provides the eventual subject-indexed preprint after endorsement.
- A journal or conference provides peer review; none of the services above
  does. The venue is IEEE TPDS (see §6).

## 1. Archive the Software on Zenodo

Create a stable GitHub release of vphilox, then archive that release on Zenodo.
Reserve the DOI before publishing so it can be added to both `CITATION.cff` and
the paper's Availability section.

The Zenodo record should describe the **software**, not the manuscript. Include
the tagged source, benchmark data, scripts, documentation, version number,
license, repository URL, and ORCID. Once checked, publish the record to activate
the DOI. See the [Zenodo DOI guide](https://help.zenodo.org/docs/deposit/describe-records/reserve-doi/).

**Done.** The record is published, holding the `v2026.08.1` source archive.

```text
10.5281/zenodo.22103483   this version, cited by the paper
10.5281/zenodo.22103482   concept DOI, always the newest version
```

The version DOI is recorded in `CITATION.cff`, `README.md`, `CHANGELOG.md` and
the paper's Availability section; the concept DOI is in `CITATION.cff` under
`identifiers`. Reserving the DOI before the tag is what let the paper cite it,
so a later release must reserve its own before rebuilding the PDF.

Do not switch on Zenodo's GitHub integration for this repository. It mints a
fresh DOI on every published GitHub Release, which would leave a second record
competing with this one and would not match the identifier already printed in
the paper.

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
  the repository can substitute for, and it is worth doing before the manuscript
  is posted anywhere, because §4 explains how the reader and the arXiv
  endorser can be the same person.

## 3. Post the Paper

**TechRxiv is closed to new submissions** as of 2026-08-26, during a migration to
a new platform. Existing content stays up and its DOIs keep resolving, but there
is no announced reopening date, so it cannot be the route. This section used to
send the manuscript there first, on the reasoning that an IEEE-operated moderated
repository beats a general-purpose one and hands back a citable DOI on the same
day.

Go to arXiv instead (§4). It was always the better home for a cs.PF paper; the
only reason it came second is that a first submission needs an endorsement, and
a TechRxiv DOI was the thing to show an endorser. **The published Zenodo record
does that job better** — an archived, reproducible artifact is a stronger claim
than a preprint identifier.

If endorsement takes long enough to be a problem, post the manuscript as a
Zenodo record of its own, resource type Publication → Preprint, kept separate
from the software record. The rule this guide used to give — never put the
manuscript on Zenodo — existed to stop a second DOI competing with the TechRxiv
one. With no TechRxiv DOI, nothing competes. What still holds is that the two
records stay distinct:

```text
10.5281/zenodo.22103482  concept DOI  -> the software, all versions
10.5281/zenodo.22103483  version DOI  -> the software at 2026.08.1
arXiv ID                              -> the paper
```

One manuscript, one identifier for it. If TechRxiv reopens after the paper is
already posted somewhere, leave it alone rather than minting a second.

## 4. Find an arXiv Endorser

Create an arXiv account, start a submission with **cs.PF (Performance)** as the
primary category, and wait for arXiv to generate an endorsement request. Use
**cs.MS (Mathematical Software)** as a suitable secondary category.

Search recent related papers in cs.PF, cs.MS, or cs.DC. On an abstract page,
select **Which authors of this paper are endorsers?** Contact one relevant
researcher at a time and provide:

- The paper itself, as a PDF or a link to a posted preprint.
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
personal address, so endorsement here is a real gate and not a formality.

**This search and the open item in step 2 are the same search.** A researcher
who publishes on PRNG design, SIMD kernels, or reproducible simulation is both a
qualified endorser and the knowledgeable reader the manuscript still needs. Ask
such a person to *read* the paper, not only to endorse it: someone who has
engaged with the work writes a far better endorsement, and may return a
correction worth having. Run the search once, for both purposes.

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

### Two constraints, both now met

- **Twelve pages.** TPDS caps review versions at 12 pages. The draft sat at
  exactly 12 and is now 11, which leaves one page of headroom. Keep it there:
  overlength charges on the accepted version are mandatory and non-negotiable,
  so a reviewer asking for one more experiment is what turns a free submission
  into a paid one.
- **The parallel result leads.** The draft's spine used to be portable
  serialized state and single-threaded throughput, with the parallel material
  two thirds of the way in. Section 8 now opens by stating the invariance
  claim — output independent of thread count, scheduling and backend — before
  any timing, and the introduction promises all three axes rather than only the
  standard-library one. Keep it that way: the hyperthreading finding and
  thread-count independence are what make this a TPDS paper and not a
  throughput report.

IEEE permits preprints on arXiv and other repositories; after acceptance the
posted version must carry the IEEE copyright notice. Neither the arXiv route in
§3 nor a Zenodo fallback record compromises a TPDS submission, and a manuscript
under review is a stronger thing to tell a prospective endorser than a preprint
link alone.
