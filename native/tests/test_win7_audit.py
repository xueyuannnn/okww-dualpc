import unittest

from tools_release.audit_win7 import optional_api_calls


class Win7AuditTests(unittest.TestCase):
    def test_dynamic_lookup_and_documentation_are_not_static_imports(self):
        source = '''// SetThreadDescription(thread, name);
/* GetDpiForWindow(window); */
auto name = "CreateFile2(path, flags)";
auto api = GetProcAddress(user, "GetDpiForWindow");
using Pointer = decltype(&GetDpiForWindow);
'''
        self.assertEqual(optional_api_calls(source), [])

    def test_actual_calls_are_reported_with_their_original_lines(self):
        source = '''/* ignored
SetThreadDescription(thread, name);
*/
auto dpi = GetDpiForWindow(window);
SetThreadDescription(thread, L"capture");
'''
        self.assertEqual(optional_api_calls(source), [
            {'api': 'GetDpiForWindow', 'line': 4},
            {'api': 'SetThreadDescription', 'line': 5},
        ])


if __name__ == '__main__':
    unittest.main()
