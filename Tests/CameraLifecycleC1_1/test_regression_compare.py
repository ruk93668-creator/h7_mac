"""Fail-closed regression-gate tests using missing, added and duplicate real ID shapes."""
from pathlib import Path
import sys,json,unittest
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
sys.dont_write_bytecode=True
sys.path.insert(0,str(ROOT/'Tests/CameraLifecycleC1'))
from regression_compare import normalize_report,compare_suite,compare_all

def row(cases,code=0,summary=None):
    raw={'cases':cases}
    if summary is not None:raw['summary']=summary
    return {'suite':'test','exit':code,'report_present':True,**normalize_report(raw)}

def case(id,passed=True,**variant):return {'id':id,'pass':passed,**variant}

class GateTests(unittest.TestCase):
    def test_missing_baseline_pass_fails(self):
        x=compare_suite(row([case(1),case(2)]),row([case(1)]));self.assertTrue(x['new_failure']);self.assertEqual(x['missing_ids'],[{'id':2}])
    def test_missing_baseline_fail_also_fails(self):self.assertTrue(compare_suite(row([case(1,False)]),row([]))['new_failure'])
    def test_added_pass_is_explicit(self):
        x=compare_suite(row([case(1)]),row([case(1),case(2)]));self.assertFalse(x['new_failure']);self.assertEqual(x['added_ids'],[{'id':2}])
    def test_added_failure_fails(self):self.assertTrue(compare_suite(row([case(1)]),row([case(1),case(2,False)]))['new_failure'])
    def test_duplicate_ids_fail(self):self.assertTrue(compare_suite(row([case(1)]),row([case(1),case(1)]))['new_failure'])
    def test_order_does_not_hide_or_create_case(self):
        x=compare_suite(row([case(1),case(2)]),row([case(2),case(1)]));self.assertFalse(x['new_failure']);self.assertTrue(x['case_results_equal'])
    def test_both_zero_without_reports_fails(self):
        a=row([]);a['report_present']=False;self.assertTrue(compare_suite(a,a)['new_failure'])
    def test_changed_id_same_count_fails(self):self.assertTrue(compare_suite(row([case(1)]),row([case(2)]))['new_failure'])
    def test_optimization_is_part_of_id(self):
        a=row([case(1,optimization='O0'),case(1,optimization='O2')]);x=compare_suite(a,a);self.assertFalse(x['new_failure']);self.assertEqual(len(x['baseline_ids']),2)
    def test_missing_optimization_fails(self):
        self.assertTrue(compare_suite(row([case(1,optimization='O0'),case(1,optimization='O2')]),row([case(1,optimization='O0')]))['new_failure'])
    def test_core_role_is_part_of_id(self):
        a=row([case(0,role='CM7',optimization='O0'),case(0,role='CM4',optimization='O0')]);self.assertFalse(compare_suite(a,a)['new_failure'])
    def test_invalid_status_not_truthy(self):
        a=row([case(0)]);b=row([{'id':0,'pass':1}]);self.assertTrue(compare_suite(a,b)['new_failure'])
    def test_summary_total_mismatch_fails(self):self.assertTrue(compare_suite(row([case(0)]),row([case(0)],summary={'total':2,'passed':1}))['new_failure'])
    def test_summary_passed_mismatch_fails(self):self.assertTrue(compare_suite(row([case(0)]),row([case(0)],summary={'total':1,'passed':0}))['new_failure'])
    def test_original_failed_case_preserved(self):
        a=row([case(0,False)],1);self.assertFalse(compare_suite(a,a)['new_failure'])
    def test_improvement_is_explicit(self):
        x=compare_suite(row([case(0,False)],1),row([case(0)]));self.assertFalse(x['new_failure']);self.assertEqual(x['improved_ids'],[{'id':0}])
    def test_exit_zero_cannot_hide_new_fail(self):self.assertTrue(compare_suite(row([case(0)]),row([case(0,False)]))['new_failure'])
    def test_known_compile_failure_can_be_compared(self):
        a=row([],1);a.update(report_present=False,diagnostic_fingerprint='Error A');self.assertFalse(compare_suite(a,a)['new_failure'])
    def test_new_compile_diagnostic_is_not_hidden(self):
        a=row([],1);b=row([],1);a['diagnostic_fingerprint']='Error A';b['diagnostic_fingerprint']='Error A\nError B';self.assertTrue(compare_suite(a,b)['new_failure'])
    def test_missing_suite_fails(self):self.assertEqual(compare_all([row([case(0)])],[])['new_failures'],1)
    def test_added_suite_is_recorded(self):
        a=row([case(0)]);x=compare_all([],[a]);self.assertEqual(x['added_suites'],['test']);self.assertEqual(x['new_failures'],0)
    def test_duplicate_suites_fail(self):
        a=row([case(0)]);self.assertTrue(compare_all([a],[a,a])['duplicate_suite_ids'])
    def test_scenario_id_not_position(self):
        a={'suite':'test','exit':0,'report_present':True,**normalize_report([{'scenario':'old_XFRC','pass':True},{'scenario':'abort_failure','pass':True}])}
        b={'suite':'test','exit':0,'report_present':True,**normalize_report([{'scenario':'old_XFRC','pass':True}])}
        self.assertTrue(compare_suite(a,b)['new_failure']);self.assertEqual(compare_suite(a,b)['missing_ids'],[{'id':'abort_failure'}])
    def test_ipc_case_id_is_qualified(self):
        a=normalize_report([{'case':0,'role':'CM7','optimization':'O0','result':'PASS'},{'case':0,'role':'CM4','optimization':'O0','result':'PASS'}]);self.assertEqual(len(a['cases']),2);self.assertFalse(a['schema_errors'])
    def test_unknown_report_not_success(self):
        a={'suite':'test','exit':0,'report_present':True,**normalize_report({'summary':{'total':1,'passed':1}})};self.assertTrue(compare_suite(a,a)['new_failure'])
    def test_added_suite_exit_zero_with_failure_fails(self):
        self.assertGreater(compare_all([],[row([case(0,False)])])['new_failures'],0)
    def test_added_suite_duplicate_cases_fail(self):
        self.assertGreater(compare_all([],[row([case(0),case(0)])])['new_failures'],0)
    def test_boolean_summary_count_is_invalid(self):
        a=row([case(0)],summary={'total':True,'passed':True});self.assertTrue(compare_suite(a,a)['new_failure'])
    def test_missing_case_id_rejected(self):
        a=row([{'pass':True}]);self.assertTrue(a['schema_errors']);self.assertTrue(compare_suite(a,a)['new_failure'])

if __name__=='__main__':
    suite=unittest.defaultTestLoader.loadTestsFromTestCase(GateTests)
    result=unittest.TextTestRunner(verbosity=2).run(suite)
    data={'total':result.testsRun,'passed':result.testsRun-len(result.failures)-len(result.errors),'failed':len(result.failures),'errors':len(result.errors),'failure_details':[str(a) for a,b in result.failures+result.errors]}
    (HERE/'regression_gate_results.json').write_text(json.dumps(data,indent=2),encoding='utf-8')
    raise SystemExit(not result.wasSuccessful())
