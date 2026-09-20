#!/usr/bin/env python3
from symbolize_object_waits import resolve_windows
symbols=[(0x81000100,0x20,'xv_object_math_lock'),(0x81000200,0x20,'actual_caller'),(0x81035200,0x20,'wrong_without_slide')]
log='''[object-locks] code anchor 81035101 symbol xv_object_math_lock; private math on
[object-locks] fast 1
[object-lock-site] lane 0 pc 81035221 count 3 wait-us 10 max-us 6 overflow 0
[object-lock-site] lane 1 pc 81039001 count 1 wait-us 2 max-us 2 overflow 0
[object-locks] code anchor 8105F101 symbol xv_object_math_lock; private math on
[object-locks] fast 1
[object-lock-site] lane 0 pc 8105F211 count 2 wait-us 7 max-us 4 overflow 0
'''
w=resolve_windows(log,symbols);assert len(w)==2
assert w[0]['load_slide']==0x35000 and w[1]['load_slide']==0x5f000
assert w[0]['sites'][0]['symbol']=='actual_caller' # LR at following symbol boundary
assert w[0]['sites'][1]['symbol'] is None
assert w[1]['sites'][0]['symbol']=='actual_caller'
for bad in [log.split('\n',1)[1].split('[object-locks] code anchor')[0],log.replace('81035101','81035105')]:
 try:resolve_windows(bad,symbols)
 except ValueError:pass
 else:raise AssertionError('Unqualified log accepted')
print('PASS: relocation, Thumb return boundary, unknown range, restart and missing/mismatched anchors')
