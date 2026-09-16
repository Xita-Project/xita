# Halo CE MCC mod-tool references

The locally installed Halo CE MCC editor kit identifies itself as
`2023.07.17.176677.1-QFE1`. Its archive contains editable campaign and Blood Gulch
tags. A private 296-file reference extraction includes 12 world BSPs, 41 models,
85 effects, 22 lights and their selected material/scenario files. Bitmap/audio
bulk was omitted. The supplied model headers use `mod2`; these are not evidence
of original Xbox model/cache compatibility.

Matching both names and tag classes against the owned Xbox maps finds 118
reference candidates for Blood Gulch, 224 for Pillar of Autumn, and 140 for the
second campaign level. Those totals include explicit `mode` to `mod2` model
references as a distinct-format case. Tags sharing a name but having different
unrelated classes are excluded. Even a matching class/name does not prove field
layout or content equivalence.

This data can help identify content and intended settings behind measured
geometry/effect costs. It does not establish equivalent Xbox memory layouts,
thread-safe query boundaries, or transferable runtime code. Candidate math
continues to be checked against the supported original Xbox image. The archive,
extracted tags and generated translations remain outside the source repository.

The private inventory and name/class correspondence reports are under
`engine-restructure-20260914T2300Z/mcc-tools-reference`. No editor executable
was launched and no Xbox map was modified during this inspection.
