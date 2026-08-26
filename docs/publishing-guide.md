# Publishing the vphilox Paper

This guide records the recommended route for making the paper and its supporting
software public as an independent researcher. Each service has a different job:

- **Zenodo** preserves the software, benchmark data, and reproducibility files.
- **TechRxiv** hosts the research paper as a computer-science preprint.
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
  the manuscript goes to TechRxiv, and it is the one nothing in the repository
  can substitute for.

## 3. Post the Paper on TechRxiv

Upload the completed PDF to [TechRxiv](https://www.techrxiv.org/). TechRxiv is
an IEEE-operated, moderated preprint repository for computer science,
engineering, and related technology. It is a better home for the manuscript
than a general-purpose Zenodo publication record and supplies a citable DOI.
See the [IEEE TechRxiv overview](https://innovate.ieee.org/techrxiv/).

Do not also upload the same manuscript to Zenodo as a separate publication.
That would create competing DOI records for one paper. Keep the records distinct:

```text
Zenodo DOI   -> software and reproducibility archive
TechRxiv DOI -> paper
arXiv ID     -> later arXiv version
```

## 4. Find an arXiv Endorser

Create an arXiv account, start a submission with **cs.PF (Performance)** as the
primary category, and wait for arXiv to generate an endorsement request. Use
**cs.MS (Mathematical Software)** as a suitable secondary category.

Search recent related papers in cs.PF, cs.MS, or cs.DC. On an abstract page,
select **Which authors of this paper are endorsers?** Contact one relevant
researcher at a time and provide:

- The TechRxiv paper and DOI.
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
