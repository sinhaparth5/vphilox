# Publishing the vphilox Paper

This guide records the recommended route for making the paper and its supporting
software public as an independent researcher. Each service has a different job:

- **Zenodo** preserves the software, benchmark data, and reproducibility files.
- **TechRxiv** hosted the paper as a computer-science preprint, and is closed
  to new submissions during a platform migration (see §3).
- **arXiv** provides the eventual subject-indexed preprint after endorsement.
- A journal or conference provides peer review; none of the services above
  does. The venue is ACM TOMS (see §6); IEEE TPDS desk-rejected the paper on
  scope in October 2026.

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

### What happened at TPDS

The paper went to IEEE Transactions on Parallel and Distributed Systems as
TPDS-2026-09-0954 and came back on 8 October 2026 without going to referees.
The Associate Editor read it, agreed with what it does, and said it belongs
somewhere else:

> The primary potential contributions of this work are in random number and
> code generation and software engineering (e.g., software libraries) and
> better suited to related venues. Thus, the manuscript is not a good fit for
> TPDS.

That is the risk this section already named, and it cost five weeks rather than
the month or two estimated. Nothing in the report questions a measurement, a
claim or the writing. The paper was not asked to change; it was asked to go to
a journal whose charter covers what it is.

### The venue is ACM TOMS

**ACM Transactions on Mathematical Software**, submitted through Manuscript
Central at <https://mc.manuscriptcentral.com/toms>. Confirm the portal from the
journal's own page before uploading, since ACM moves these.

TOMS is where this paper's own closest references were published. TestU01
(L'Ecuyer and Simard, 2007) is a TOMS paper and so is xoshiro256++ (Blackman
and Vigna, 2021); the Mersenne Twister went to TOMACS in 1998. Those three are
cited in Sections 2, 7 and 9. A venue that published the generator this paper
measures against, the battery it is validated with, and the competitor it
reports losing to is not going to call the subject out of scope.

TOMACS remains the sibling fallback and its charter names random number
generation outright. Take TOMS first: the TPDS editor's own words were "random
number and code generation and software engineering (e.g., software
libraries)", and software for mathematical computation is the TOMS charter
rather than the TOMACS one.

### Submit as a regular paper, not an Algorithm

TOMS runs a separate **Algorithms** track whose papers deposit their software
into the ACM Collected Algorithms and receive an algorithm number. Do not use
it here. The CALGO deposit carries ACM's own software terms, and this library
is MIT OR Apache-2.0 on GitHub and archived on Zenodo under the same terms. A
regular research paper cites the repository and the DOI, keeps the licence, and
loses nothing the paper is arguing for.

### The APC is back, and it is the real cost

ACM became a fully open-access publisher on 1 January 2026 and retired the
subscription track, so an accepted TOMS paper carries a mandatory APC. This was
the whole reason TPDS was chosen first. Reported 2026 journal rates are $1,300
and $1,800 list, subsidised to $950 and $1,450, but the figures come from
secondary sources and ACM's current price list is the one that binds.

ACM's discretionary waiver policy says that stating an inability to pay is not
by itself sufficient, and that ACM weighs whether the author's institution could
reasonably be expected to join ACM Open. An author with no institution at all is
a better case under that second test than a funded one at a non-participating
university, not a worse one, but it is still discretionary and it is decided
after acceptance, during the e-rights process. Geographic waivers do not apply
to the United Kingdom.

So the sequence is: submit, get reviewed, and only then find out the price. A
rejection costs nothing. An acceptance may cost up to about $1,450, and the
waiver request is made at that point and not before. Decide now whether that
outcome is acceptable, because withdrawing an accepted paper over the APC wastes
the referees' time as well as your own.

### What the template change did and did not do

The manuscript moved from IEEEtran to `acmart` in the `acmsmall` format. Only
the class and the reference handling changed: the references became a `.bib`
for ACM-Reference-Format, CCS concepts and ACM keywords replaced the IEEE index
terms, and the double-column floats became single-column ones. No measurement,
claim or section moved.

TOMS sets no page limit for a regular submission, so the two constraints this
section used to track are gone. The twelve-page cap does not apply, and the
parallel result no longer has to lead for scope reasons. Leaving Section 8 where
it is costs nothing, so leave it: thread-count independence is still the
property the library exists for.

ACM permits preprints on arXiv and other repositories. Neither the arXiv route
in Section 3 nor a Zenodo fallback record compromises a TOMS submission, and a
manuscript under review is a stronger thing to tell a prospective endorser than
a preprint link alone.
