"""Guard against empty, skipped or malformed reports being treated as passes."""
import pathlib,tempfile,unittest,xml.etree.ElementTree as ET
from m6_validation import classify_junit
class Accounting(unittest.TestCase):
    def check(self,body,expected):
        with tempfile.TemporaryDirectory() as d:
            p=pathlib.Path(d)/'result.xml';p.write_text(body)
            self.assertEqual(classify_junit(p,'target'),expected)
    def test_pass(self):self.check('<testsuite><testcase name="target" status="run"/></testsuite>','pass')
    def test_failure(self):self.check('<testsuite><testcase name="target"><failure/></testcase></testsuite>','fail')
    def test_error(self):self.check('<testsuite><testcase name="target"><error/></testcase></testsuite>','fail')
    def test_skip(self):self.check('<testsuite><testcase name="target"><skipped/></testcase></testsuite>','skip')
    def test_notrun(self):self.check('<testsuite><testcase name="target" status="notrun"/></testsuite>','skip')
    def test_empty(self):self.check('<testsuite/>','missing')
    def test_wrong_name(self):self.check('<testsuite><testcase name="other"/></testsuite>','missing')
    def test_duplicate(self):self.check('<testsuite><testcase name="target"/><testcase name="target"/></testsuite>','missing')
    def test_failure_beats_skip(self):self.check('<testsuite><testcase name="target"><skipped/><failure/></testcase></testsuite>','fail')
    def test_malformed(self):
        with self.assertRaises(ET.ParseError):self.check('not XML','pass')
if __name__=='__main__':unittest.main()
