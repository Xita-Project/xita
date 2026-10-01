#!/usr/bin/env python3
"""Validate sample accounting without proprietary logs or generated game code."""
import unittest
from summarize_object_samples import summarize

READY='frame stats: loaded 1 active 1 director on 1\n'
HEADER='[object-sample] 60 frames roots seen 100 selected 2 open 0;\n'
ROWS='[tick-phases] F0900002>8FB70 1.00 (2) 8FB70>8FB70 0.30 (1) 8FB70>8DDF0 0.40 (3)\n'

class Summary(unittest.TestCase):
    def test_recursive_time_not_counted_twice(self):
        r=summarize(READY+HEADER+ROWS+HEADER)
        self.assertEqual(r['roots_sampled'],2)
        self.assertEqual(r['sampled_root_elapsed_ms'],60)
        self.assertEqual(r['direct_components']['8DDF0']['sampled_elapsed_ms'],24)
    def test_type_callback_denominator(self):
        extra='[tick-phases] 8FB70>90950 0.50 (2) 90950>4C980 0.25 (1)\n'
        r=summarize(READY+HEADER+ROWS+extra+HEADER)
        self.assertEqual(r['type_callbacks']['4C980']['sampled_elapsed_ms'],15)
        self.assertEqual(r['type_callbacks']['4C980']['fraction_of_sampled_type_update_time'],0.5)
    def test_unclosed_report_not_used(self):
        self.assertEqual(summarize(READY+HEADER+ROWS)['complete_valid_windows'],0)
    def test_open_tree_rejected(self):
        r=summarize(READY+HEADER.replace('open 0','open 1')+ROWS+HEADER)
        self.assertEqual(r['rejected_gameplay_windows'],1)
    def test_truncated_counts_rejected(self):
        r=summarize(READY+HEADER+ROWS.replace('(2)','(1)')+HEADER)
        self.assertEqual(r['rejected_gameplay_windows'],1)
    def test_menu_excluded(self):
        self.assertEqual(summarize(HEADER+ROWS+HEADER)['complete_valid_windows'],0)

if __name__=='__main__':unittest.main()
