# TOMS submission pack

Portal: https://mc.manuscriptcentral.com/toms
(Confirm it from https://dl.acm.org/journal/toms first. ACM moves these, and the
journal page always carries the current "Submit" link.)

## Files in this directory

  vphilox-toms.pdf          upload as the main document (PDF)
  vphilox-toms-source.zip   upload as the LaTeX source
  vphilox-cover-letter.pdf  upload as the cover letter

The zip holds vphilox.tex, vphilox.bib, vphilox.bbl, acmart.cls,
ACM-Reference-Format.bst and figures/ (three PDFs). No .pdf of the paper inside.
It is test-compiled from a clean directory, with a real BibTeX pass, and
reproduces the uploaded PDF byte for byte: 20 pages, no overfull boxes, no
undefined references.


## Template

  \documentclass[acmsmall,screen,review]{acmart}

acmsmall is the ACM small-format journal layout TOMS uses. "review" numbers the
lines for referees. "anonymous" is deliberately absent: TOMS referees are
anonymous to the author, not the author to the referees, and the XGBoost
conflict is disclosed in a Section 1 footnote that anonymising would remove.

The class and the .bst are vendored in the zip at acmart v2.20. TeX Live 2025
ships v2.16, and ACM asks for the current one.


## 1. Manuscript type

  Regular paper (research paper)

NOT the Algorithms track. An Algorithms paper deposits its software into the ACM
Collected Algorithms under ACM's own terms; vphilox is MIT OR Apache-2.0 on
GitHub and archived on Zenodo, and the regular track cites that record instead.

TOMS sets no page limit on a regular submission, so the 20 pages are fine.


## 2. Open access and the APC

ACM has had no subscription track since 1 January 2026, so acceptance carries a
mandatory APC. Reported 2026 journal rates are $1,300 and $1,800 list,
subsidised to $950 and $1,450; ACM's own price list is the one that binds.

There is nothing to decide at submission. The charge is handled after acceptance
during the e-rights process, which is also when a discretionary waiver is
requested. ACM weighs whether the author's institution could reasonably join ACM
Open; having no institution is a better case under that test than a funded post
at a non-participating university, but it stays discretionary. Geographic
waivers do not cover the United Kingdom.

Decide before submitting whether an acceptance that costs up to roughly $1,450
is acceptable. Withdrawing an accepted paper over the APC wastes the referees'
time.


## 3. Title

Portable, Seekable Random Streams for Parallel CPU Simulation


## 4. Abstract

The full version is 270 words. If a shorter one is needed, cut from "The cost of
those properties" onward.

A long simulation that checkpoints its random generator and resumes on a
different machine is relying on a property that std::mt19937 does not have. The
C++ standard fixes the text format of an engine's stream operators only
partially, and the three major standard libraries disagree on both the numeric
base of the digits and the order of the 624 state words. A checkpoint written
under libstdc++ fails to load under the MSVC standard library, and loads without
any diagnostic, at the wrong position, under libc++. This paper describes
vphilox, a header-only C++20 library that removes the failure by changing what is
stored. The generator is Philox4x32-10, a counter-based construction whose entire
state is a key and a position, so a checkpoint is six 32-bit integers and a word
offset, not a snapshot of engine internals. The same property gives
constant-time seek and gives each worker an independent substream with no
coordination, so a task draws the same numbers whatever the thread count and
schedule. The cost of those properties is the question the design has to answer,
because the earlier attempt to adopt Philox in a production machine-learning
library was rejected on the grounds that scalar Philox runs at one tenth the
speed of the Mersenne Twister. We report that the tenfold penalty did not
reproduce on any of five processors measured, and that interleaving independent
counters across SIMD lanes brings the generator to between 0.22x and 0.75x the
cost per byte of std::mt19937 on the bulk path. Aggregate cost per byte is flat
across thread counts, and the first parallel limit we find is hyperthread
co-location, not the generator.


## 5. CCS concepts

These are in the manuscript already, as \ccsdesc lines. The submission form may
ask for them again:

  500  Mathematics of computing -> Mathematical software
  500  Software and its engineering -> Software libraries and repositories
  300  Mathematics of computing -> Probabilistic algorithms
  300  Computer systems organization -> Single instruction, multiple data
  100  Mathematics of computing -> Mathematical software performance
  100  Computing methodologies -> Parallel algorithms

The concept_id numbers in the CCSXML block were taken from published ACM papers
rather than retyped, because they are not guessable. Regenerate the block from
https://dl.acm.org/ccs at camera ready, which is when ACM asks for it anyway.
That is also the moment to add "Random number generation" if the tool offers it
under Probability and statistics; it was left out rather than invented.


## 6. Keywords

Counter-based random number generation, Philox, portable serialized state,
constant-time seek, SIMD vectorization, reproducible parallel streams, thread
scaling, C++20, statistical testing


## 7. Authors and affiliations

  Title                Mr.
  Name                 Parth Kumar Sinha
  Email                sinhaparth555@gmail.com
  ORCID                0009-0002-3120-9301
  Country              United Kingdom
  Corresponding        yes

Affiliation: independent researcher, Oxford, United Kingdom. Do not attach an
organisation you are not at. Under ACM Open the affiliation field decides
whether an institution's agreement covers the APC, so an invented one would be a
billing claim as well as a false statement.


## 8. Funders

No funding was received for this research.


## 9. Additional information

  Previously submitted to this journal?   No
  Previously published, in whole or part? No
  Informed consent / animal subjects      No human or animal subjects
  Opposed reviewers                       none
  Code associated with the manuscript?    Yes
  Data associated with the manuscript?    Yes, and it will be shared
    - link in the manuscript                        (Availability section)
    - uploaded to a data repository                 (Zenodo)

Data repository block, exactly:

  Repository  Zenodo
  DOI         10.5281/zenodo.22103483
  Title       vphilox: SIMD-Accelerated Counter-Based Pseudo-Random Number
              Generation for Parallel CPU Systems

The DOI field takes the bare identifier. A "doi:" prefix or a full URL fails
validation. The Title field wants the dataset's title, not a link.

TOMS asks for a complete software package, and this one is already public. If
the form offers a replicated-computational-results review, accept it: every
figure regenerates from archived JSON under CI, and every PractRand and TestU01
log carries the git SHA of the binary that wrote it.

Do not volunteer the TPDS history. It was a scope decision by one editor, no
referee saw the paper, and nothing in it was asked to change. ACM does not ask.


## 10. Final review

  - the page count reads 20 in the portal's own proof, not just in your PDF
  - figures are legible in that proof; it re-renders and can downsample
  - the byline under the title reads "Parth Kumar Sinha"
  - manuscript type is the regular research track, not Algorithms
  - ORCID is attached
  - the CCS concepts block survived the upload

Manuscript Central emails a manuscript ID. Keep it.


## Zenodo

  Concept DOI  10.5281/zenodo.22103482   cites vphilox in general
  Version DOI  10.5281/zenodo.22103483   cites the release this paper measures

Use the version DOI in the submission. It is what the Availability section
cites, and a reviewer checking reproducibility needs the release the numbers
came from.

The Zenodo record still lists the creator as "Sinha, Parth". Zenodo allows
editing metadata on a published record without minting a new DOI, so that can be
corrected to "Sinha, Parth Kumar" to match the paper.


## After submitting

Consider posting the preprint to Preprints.org so the work has a public
timestamp while review runs. ACM permits preprints, and the same PDF works.
