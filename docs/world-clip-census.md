# World draw coverage diagnostic

The settled a30 pod census found 19 of 20 draws for each of `halo_vs_16`
and `halo_vs_40` produced no samples. Zero samples alone cannot distinguish
frustum rejection, backface rejection, depth/stencil rejection or fragment
coverage. It does not justify omitting the draw next frame.

The perf223 candidate adds `[clip-census]` alongside the existing
`XV_FRAG_CENSUS=120` reports. It leaves every draw and its order intact.
It reports eligible draw count, draws whose referenced vertices share an
outside x/y homogeneous clip plane, their index count, and how many of those
had nonzero GPU coverage. Any nonzero coverage contradicts the classifier's
assumptions and must be investigated before using it for draw admission.
Zero contradictions in one view are not a general correctness proof.

Only the two audited raw-v0 affine position shaders are admitted. Other
programs, including `halo_vs_11` with a derived position register, are excluded.
The captured position must be F32 xyz at stream-zero offset zero; its actual
retained stride must match the replay layout. Immediate streams are excluded.
Captured byte extents account for the packed 16-byte representation. The test
reads only referenced indices, permitting the existing sparse upload path.
The first four captured constant rows provide the affine x/y/w transform.
No depth-plane convention is assumed.

Classification occurs in census completion after the packet's GPU fence and
before frame storage retirement, using the cached upload mirror when present.
It uses explicit extent checks and memcpy for potentially unaligned reads.
Nonfinite input and boundary uncertainty fail classification. The deliberately
broad numerical margin is not a certified SGX arithmetic error bound.
This is diagnostic CPU work, so frame times with it enabled are not clean
performance measurements. Census disabled performs no vertex classification;
each captured draw retains two additional extent/stride words.

Host ASan/UBSan and ARM fixtures check all four x/y planes, bounds failures,
invalid indices, nonfinite values, boundary cases, mixed outside planes and
unaligned packed inputs. Hardware coverage comparison is still pending.
