# Public release preparation audit

**September 8, 2026 — local preparation only. Publication is not authorized.**

The GPL license, documentation and release tools are being prepared for review.
The current private checkout and its history are **not cleared for public
release**. This is a technical inventory and risk review, not a legal opinion
or a guarantee against claims. A qualified IP lawyer should review the intended
distribution, provenance and applicable jurisdiction before publication.

## Findings

The initial inventory inspected 2,374 tracked paths and reachable Git objects.
Heuristics flagged 2,278 current files and 3,513 historical blobs for review;
these are overlapping categories of risk, not findings that every file infringes.
After replacing the embedded code signature with a hash, 2,277 tracked paths
remain flagged. The original historical findings remain unchanged.
No credential matched the implemented patterns in that pass; this is not a
comprehensive secret scan or proof that no secret exists.

| Finding | Action and remaining work |
| --- | --- |
| No project software license | Added full GPL v3 text, an explicit GPL-3.0-only notice and an offline dashboard license reader. The grant applies to original code, not third-party game content. |
| Translated game shaders, raw definitions and compiled programs tracked | Exclude them from the source review export. Establish a reproducible local generation path and provenance before deciding what can be distributed. |
| Game screenshots, video and artwork tracked | Hold out of the source review export. Review each asset and its permitted use separately; an independent project name does not license captured game imagery. |
| Game manifest and symbol export tracked | Regenerate game metadata locally. Record the symbol export's exact upstream revision and generation method; retain upstream notices. |
| An 84-byte game-code recognition sequence embedded in `xita_recomp.py` | Replaced with a SHA-256 check over the user's input region. This preserves recognition without shipping that original byte sequence. It does not establish provenance of all other code. |
| Old README claimed no distributed game content and logs containing no game data | Corrected. Logs can contain captured shader definitions, memory details and local paths. |
| Ignored generated files can still exist in history | Expanded ignore rules and added tracked/history inspection. No history was rewritten and no original data was deleted. |
| Clean-source Halo build not reproducible yet | The current build consumes game-derived shader inputs and unverified symbol metadata. Finish input regeneration and validate a fresh build before calling a source release complete. |

## License and legal boundaries

The chosen license is [GPL-3.0-only](../LICENSE). Its source-distribution and
corresponding-source obligations matter when distributing covered binaries;
it does not supply rights to proprietary code linked or translated into them.
See the [GNU GPL v3 text](https://www.gnu.org/licenses/gpl-3.0.html).

Game algorithms and interfaces need to be distinguished from copied expression.
The U.S. Copyright Office explains that computer-program protection covers
copyrightable expression and that a claim in new derivative-program material
does not cover third-party authorship. Applying that distinction to this
recompiler and its translations needs a provenance review, not a blanket
"reverse engineering is legal" assertion.
[Copyright Office Circular 61](https://www.copyright.gov/circs/circ61.pdf).

Owning a game copy is not a blanket redistribution authorization. Circumvention
and interoperability exceptions have specific conditions; their applicability
must be assessed separately from copyright and licensing.
[17 U.S.C. §1201](https://www.copyright.gov/title17/92chap12.html#1201).

Microsoft's [Game Content Usage Rules](https://www.xbox.com/en-US/developers/rules)
must not be treated as general permission to publish a recompiled game or its
source. Public media, branding and any reliance on those rules need their own
review. No Microsoft, Sony or game-publisher endorsement is claimed.

The audit found references to open tools and specifications; those references
do not prove that every file was independently authored. The contributor must
record any copied/adapted implementation and its actual license. See
[THIRD_PARTY.md](../THIRD_PARTY.md) and [CONTRIBUTING.md](../CONTRIBUTING.md).

## Re-run the local checks

```sh
python3 tools/release_audit.py --history --json release-audit.json
```

Exit status 1 means review findings remain; it is expected in the current
development checkout. Reports identify paths/categories and do not print secret
values. The history pass inspects reachable blobs and their representative
paths. It does not audit unreachable objects, external attachments, LFS storage,
remote branches absent locally or already-published copies.

Create a separate source review directory outside this checkout:

```sh
python3 tools/release_audit.py --export ../xita-source-review --json release-audit.json
```

The destination must not exist. It has no Git history, omits flagged/unknown
file types, and includes an inventory of included and excluded paths. This is
a **review artifact**, not a legal clearance or a complete Halo build. Private
development files remain intact. The tool never pushes, publishes, changes
repository visibility or rewrites history.

## Gates before publication

1. Resolve source, symbol, shader and media provenance; obtain legal review of
   distribution and compatibility techniques where needed.
2. Complete and test game-input regeneration from a supported user-owned copy;
   verify an independent fresh checkout/build, including the Windows workflow.
3. Verify the exact files and corresponding source of any proposed artifact.
   Do not include a Halo-linked executable without the necessary rights.
4. Choose a clean source snapshot or a separately reviewed history cleanup.
   Never flip this development repository public assuming `.gitignore` removed history.
5. Re-run the audit and dependency/secret review, review PR requirements and
   compatibility claims, and obtain explicit publication authorization.
