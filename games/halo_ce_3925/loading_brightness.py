"""Keep the loading animation's fractional-power base in its real domain.

Hardware captured finite negative smoothed progress after the animation phase
entered its negative half-cycle. The original 0.9-power then produces NaN RGB.
Only this presentation call is guarded; no gameplay or general pow semantics
change. Positive values follow the original calculation unchanged.
"""
ADDRESS = 0xD4D09
MARKER = '/* Xita: loading brightness real-domain guard. */'
LINES = [
    '    ' + MARKER,
    '    if (X_ST(1) < 0.0) X_ST(1) = 0.0;',
]


def before_instruction(address, enabled):
    return list(LINES) if enabled and address == ADDRESS else []
