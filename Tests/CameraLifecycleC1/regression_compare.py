"""Strict report normalization and ID-set regression comparison; no runner side effects."""
import json

# A test ID is qualified by its actual execution variant, never by its row position.
def _identity(case):
    value = case.get('id', case.get('case', case.get('scenario', case.get('name'))))
    if type(value) not in (int, str) or value == '':
        raise ValueError('missing or invalid case ID')
    result = {'id': value}
    for key in ('role', 'optimization', 'mode', 'variant', 'configuration', 'speed'):
        if key in case:
            result[key] = case[key]
    return result

# Accept explicit boolean or PASS/FAIL outcomes, not arbitrary truthy values.
def _outcome(case):
    for key in ('pass', 'passed', 'result'):
        if key in case:
            value = case[key]
            if type(value) is bool:
                return value
            if type(value) is str and value in ('PASS', 'FAIL'):
                return value == 'PASS'
            raise ValueError('invalid case outcome')
    raise ValueError('missing case outcome')

# Preserve external unittest summaries without pretending they describe embedded core_cases.
def normalize_report(raw):
    errors = []
    if isinstance(raw, list):
        cases, summary, source = raw, {}, 'cases'
    elif isinstance(raw, dict):
        summary = raw.get('summary', {})
        source = next((key for key in ('cases', 'tests', 'core_cases') if key in raw), None)
        cases = raw.get(source, []) if source else []
    else:
        return {'cases': [], 'summary': {}, 'schema_errors': ['invalid report root'], 'cases_source': None}
    if not isinstance(summary, dict):
        errors.append('invalid summary'); summary = {}
    for key in ('total', 'executions', 'passed', 'failed', 'failures', 'errors', 'unique_cases'):
        if key in summary and (type(summary[key]) is not int or summary[key] < 0):
            errors.append('invalid summary count: ' + key)
    if not isinstance(cases, list):
        errors.append('invalid cases array'); cases = []
    values = []
    for index, case in enumerate(cases):
        try:
            if not isinstance(case, dict):
                raise ValueError('invalid case record')
            values.append({'id': _identity(case), 'pass': _outcome(case),
                           'failure': case.get('failure_line', case.get('failed_line', case.get('fail_line')))})
        except ValueError as ex:
            errors.append(f'row {index}: {ex}')
    if source != 'core_cases':
        expected = summary.get('executions', summary.get('total'))
        if expected is not None and expected != len(values):
            errors.append('summary count differs from complete case list')
        passed = sum(c['pass'] for c in values)
        if 'passed' in summary and summary['passed'] != passed:
            errors.append('summary passed count differs from case list')
        if 'failed' in summary and summary['failed'] != len(values) - passed:
            errors.append('summary failed count differs from case list')
    if not summary and source:
        summary = {'total': len(values), 'passed': sum(c['pass'] for c in values),
                   'failed': sum(not c['pass'] for c in values)}
    return {'cases': values, 'summary': summary, 'schema_errors': errors, 'cases_source': source}

# Duplicate IDs invalidate comparison instead of overwriting one case with another.
def _index(row):
    result, duplicates = {}, []
    for case in row.get('cases', []):
        key = json.dumps(case['id'], sort_keys=True, ensure_ascii=False)
        if key in result:
            duplicates.append(case['id'])
        else:
            result[key] = case
    return result, duplicates

# Compare full sets and every baseline outcome; process exits alone cannot establish equivalence.
def compare_suite(baseline, current):
    old, old_duplicates = _index(baseline)
    new, new_duplicates = _index(current)
    missing = [old[k]['id'] for k in sorted(old.keys() - new.keys())]
    added = [new[k]['id'] for k in sorted(new.keys() - old.keys())]
    shared = old.keys() & new.keys()
    regressed = [old[k]['id'] for k in sorted(shared) if old[k]['pass'] and not new[k]['pass']]
    improved = [old[k]['id'] for k in sorted(shared) if not old[k]['pass'] and new[k]['pass']]
    added_failed = [new[k]['id'] for k in sorted(new.keys() - old.keys()) if not new[k]['pass']]
    errors = list(baseline.get('schema_errors', [])) + list(current.get('schema_errors', []))
    for name, row in [('baseline', baseline), ('current', current)]:
        if row['exit'] == 0 and (not row.get('report_present') or not row.get('cases')):
            errors.append(name + ' exited 0 without a complete nonempty case report')
    equal = old.keys() == new.keys() and all(old[k] == new[k] for k in shared)
    both_unavailable = not old and not new and baseline['exit'] != 0 and current['exit'] != 0
    diagnostic_changed = both_unavailable and baseline.get('diagnostic_fingerprint') != current.get('diagnostic_fingerprint')
    summary_changed = baseline.get('summary', {}) != current.get('summary', {})
    # With no case changes explaining it, changed summary totals/errors cannot silently pass.
    unexplained_summary = summary_changed and not (missing or added or regressed or improved)
    failure = bool(missing or regressed or added_failed or old_duplicates or new_duplicates or errors or
                   (baseline['exit'] == 0 and current['exit'] != 0) or diagnostic_changed or unexplained_summary)
    return {'suite': baseline['suite'], 'baseline_exit': baseline['exit'], 'current_exit': current['exit'],
            'baseline_summary': baseline.get('summary', {}), 'current_summary': current.get('summary', {}),
            'baseline_ids': [old[k]['id'] for k in sorted(old)], 'current_ids': [new[k]['id'] for k in sorted(new)],
            'missing_ids': missing, 'added_ids': added, 'regressed_ids': regressed, 'improved_ids': improved,
            'baseline_duplicate_ids': old_duplicates, 'current_duplicate_ids': new_duplicates,
            'schema_errors': errors, 'case_results_equal': equal, 'summary_changed': summary_changed,
            'diagnostic_changed': diagnostic_changed, 'new_failure': failure,
            'baseline_diagnostic': baseline.get('diagnostic_tail', ''), 'current_diagnostic': current.get('diagnostic_tail', '')}

# Missing or repeated suite IDs are also regression failures, not truncated by zip().
def compare_all(baseline, current):
    a = {r['suite']: r for r in baseline}; b = {r['suite']: r for r in current}
    missing = sorted(a.keys() - b.keys()); added = sorted(b.keys() - a.keys())
    duplicates = len(a) != len(baseline) or len(b) != len(current)
    comparisons = [compare_suite(a[k], b[k]) for k in sorted(a.keys() & b.keys())]
    added_failed = any(r['exit'] != 0 or r.get('schema_errors') or not r.get('cases') or not r.get('report_present') or _index(r)[1] or any(not c['pass'] for c in r.get('cases', [])) for k, r in b.items() if k in added)
    return {'comparison': comparisons, 'missing_suites': missing, 'added_suites': added,
            'duplicate_suite_ids': duplicates, 'added_suite_cases': {k: b[k].get('cases', []) for k in added},
            'new_failures': sum(c['new_failure'] for c in comparisons) + bool(missing or duplicates or added_failed)}
