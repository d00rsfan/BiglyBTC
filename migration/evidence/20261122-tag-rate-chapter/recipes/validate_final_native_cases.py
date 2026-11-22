"""Keep the initial failed run and associate corrected retests explicitly."""
from pathlib import Path
import copy,hashlib,json,re,xml.etree.ElementTree as ET

D=Path(__file__).resolve().parent
base=list(ET.parse(D/'native-current-tests.xml').getroot().iter('testcase'))
retests=list(ET.parse(D/'native-retests-r2.xml').getroot().iter('testcase'))
rows={row.attrib['name']:row for row in base}
assert len(rows)==len(base)==len(json.loads((D/'selected-native-tests.json').read_text()))
failed=[name for name,row in rows.items() if row.find('failure')is not None or row.find('error')is not None]
assert failed and set(failed)<=set(r.attrib['name']for r in retests)
for row in retests:
    assert row.attrib['name']in rows
    assert all(row.find(kind)is None for kind in ['failure','error','skipped']),row.attrib
    rows[row.attrib['name']]=row
qt_passed=0
for name,row in rows.items():
    assert all(row.find(kind)is None for kind in ['failure','error','skipped']),name
    for passed,failures,skipped in re.findall(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped',''.join(row.itertext())):
        assert failures=='0'and skipped=='0',(name,failures,skipped)
        qt_passed+=int(passed)
merged=ET.Element('testsuite',name='affected-native-cases-after-explicit-corrections',
                  tests=str(len(rows)),failures='0',skipped='0')
for row in rows.values():merged.append(copy.deepcopy(row))
ET.ElementTree(merged).write(D/'native-final-tests.xml',encoding='UTF-8',xml_declaration=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
result=dict(status='PASS_ALL_AFFECTED_NATIVE_CASES',cases=len(rows),qt_rows_visible_in_junit=qt_passed,
            qt_output_audit=json.loads((D/'native-qt-output-audit.json').read_text()),
            initial_failed_case_names=failed,retested_case_names=[r.attrib['name']for r in retests],
            initial_xml_sha256=sha(D/'native-current-tests.xml'),
            retest_xml_sha256=sha(D/'native-retests-r2.xml'),
            merged_xml_sha256=sha(D/'native-final-tests.xml'),
            initial_run_retained=True,no_initial_all_pass_claim=True)
(D/'native-final-validation.json').write_text(json.dumps(result,indent=2)+'\n')
print('PASS_ALL_'+str(len(rows))+'_AFFECTED_NATIVE_CASES;explicit corrected retests,initial failures retained;CTest skipped=0')
