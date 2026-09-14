# TPDS submission pack

Portal: https://mc.manuscriptcentral.com/tpds-cs
(If that does not resolve to TPDS, go to https://www.computer.org/csdl/journal/td
and use "Submit a Paper".)

## Files in this directory

  vphilox-tpds.pdf          upload as Main Document - PDF
  vphilox-tpds-source.zip   upload as Main Document - LaTeX
  vphilox-cover-letter.pdf  upload as Cover letter / Comments

The zip holds vphilox.tex, IEEEtran.cls and figures/ (three PDFs). No .pdf
inside. It is test-compiled from a clean directory and reproduces the uploaded
PDF byte for byte: 12 pages, no overfull boxes, no undefined references.

## Template

  \documentclass[lettersize,journal]{IEEEtran}

This is the sample file in IEEE's Computer_Society_LaTeX_template.zip. It is NOT
[10pt,journal,compsoc], which the how-to in that same zip still lists for
Computer Society journals: the editorial office rejects compsoc submissions.
The sample file binds, not the how-to.


## 1. Article Type

  Regular Paper

The 12-page limit applies to this type and the manuscript is 12 pages. Do not
pick Open Access unless you intend to pay the APC; traditional submission
carries no charge.


## 2. Peer Review Type

  Single anonymized

Identifying information stays in the manuscript, which is where the XGBoost
conflict is already disclosed in a footnote.


## 3. Title

Portable, Seekable Random Streams for Parallel CPU Simulation


## 4. Abstract

ScholarOne accepted the full 270-word version. If a shorter one is ever needed,
cut from "The cost of those properties" onward.

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


## 5. Keywords

The picker is the ACM CCS category tree, not the IEEE Thesaurus. Six, all in
trees that match the paper:

  C.1.2  Multiple Data Stream Architectures (Multiprocessors)
  C.4    Performance of Systems
  D.1.3  Concurrent Programming
  D.2m.a Software libraries
  D.4.5  Reliability
  G.3.j  Random number generation

Do not use B.1.5.b (microcode), H.5.2.b (benchmarking under HCI) or I.3.1.d
(parallel processing under Computer Graphics). Right words, wrong disciplines,
and editors pick reviewers partly from these.


## 6. Authors and affiliations

  Title                Mr.
  Name                 Parth Kumar Sinha
  Email                sinhaparth555@gmail.com
  ORCID                0009-0002-3120-9301
  Country              United Kingdom
  Corresponding        yes

Affiliations: leave "Not provided". That field only determines eligibility for
institutional APC discounts, and the traditional track carries no charge. Do not
attach an organisation you are not at.


## 7. Funders

No funding was received for this research.


## 8. Additional Information

  Previously submitted to this journal?   No
  Previously published, in whole or part? No
  Informed consent / animal subjects      No human or animal subjects
  Opposed reviewers                       none
  Code associated with the manuscript?    Yes
  Data associated with the manuscript?    Yes, and it will be shared
    - I have included a link in my manuscript        (Availability section)
    - I have uploaded my data to a data repository   (Zenodo)

Data repository block, exactly:

  Repository  Zenodo
  DOI         10.5281/zenodo.22103483
  Title       vphilox: SIMD-Accelerated Counter-Based Pseudo-Random Number
              Generation for Parallel CPU Systems

The DOI field takes the bare identifier. A "doi:" prefix or a full URL fails
validation. The Title field wants the dataset's title, not a link.


## 9. Final Review

  - the page count reads 12 in ScholarOne's own proof, not just in your PDF
  - figures are legible in that proof; it re-renders and can downsample
  - the byline under the title reads "Parth Kumar Sinha"
  - article type is Regular and Open Access is not selected
  - ORCID is attached

ScholarOne emails a manuscript ID. Keep it.


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
timestamp while review runs. TPDS permits preprints, and the same PDF works.
