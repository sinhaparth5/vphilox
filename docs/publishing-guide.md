# Publishing the vphilox Paper

This guide records the recommended route for making the paper and its supporting
software public as an independent researcher. Each service has a different job:

- **Zenodo** preserves the software, benchmark data, and reproducibility files.
- **TechRxiv** hosted the paper as a computer-science preprint, and is closed
  to new submissions during a platform migration (see §3).
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
  citations, methodology, figures, and language. This is the last item before
  the manuscript is posted anywhere, and it is the one nothing in the repository
  can substitute for.

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
explains the current rules.

## 5. Submit to arXiv

After endorsement, upload the LaTeX source and figure PDFs rather than only the
locally generated PDF. Run `paper/stage-figures.sh`, package `vphilox.tex` and
`figures/*.pdf`, then inspect arXiv's generated PDF carefully before completing
the submission. Follow the [official submission guide](https://info.arxiv.org/help/submit/index.html).

Finally, check the preprint policy of any intended journal or conference before
submission. Posting a preprint does not itself constitute peer-reviewed
publication.
