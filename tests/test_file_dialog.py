# SPDX-License-Identifier: LGPL-2.1-or-later
"""release/windows/raye-file-dialog: the file dialog a Windows program gets
when it asks for a file or a folder.

    python3 -I -B -m unittest discover -s tests

kdialog is replaced by a small script on PATH that writes down how it was
called and answers what the test tells it to.
"""
import importlib.machinery
import importlib.util
import os
import pathlib
import stat
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
TOOL = ROOT / "release" / "windows" / "raye-file-dialog"
PATCH = ROOT / "patches" / "base" / "0021-raye-file-dialog.patch"
sys.dont_write_bytecode = True


def load():
    loader = importlib.machinery.SourceFileLoader("raye_file_dialog", str(TOOL))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    return mod


fd = load()

FAKE = """#!/bin/sh
printf '%s\\n' "$@" > "$FAKE_ARGS"
printf '%s\\n' "${QT_QPA_PLATFORM:-}" > "$FAKE_ARGS.platform"
[ -n "${FAKE_OUT:-}" ] && printf '%s\\n' "$FAKE_OUT"
exit ${FAKE_RC:-0}
"""


class Filters(unittest.TestCase):
    def test_windows_patterns_become_qt_ones(self):
        self.assertEqual(fd.qt_filter(["Text files|*.txt;*.log", "All files|*.*"], 1),
                         "Text files (*.txt *.log);;All files (*)")

    def test_chosen_filter_comes_first(self):
        self.assertEqual(fd.qt_filter(["Text|*.txt", "Pictures|*.png;*.jpg"], 2),
                         "Pictures (*.png *.jpg);;Text (*.txt)")

    def test_name_that_already_lists_the_patterns(self):
        self.assertEqual(fd.qt_filter(["Normal text file (*.txt)|*.txt"], 1), "Normal text file (*.txt) (*.txt)")

    def test_no_filter(self):
        self.assertEqual(fd.qt_filter([], 1), "")

    def test_odd_text_cannot_break_the_list(self):
        self.assertEqual(fd.qt_filter(["A;;B|*.x"], 1), "A;B (*.x)")
        self.assertEqual(fd.qt_filter(["Only a name|"], 1), "Only a name (*)")
        self.assertEqual(fd.qt_filter(["|*.x)"], 1), "* (*)")

    def test_out_of_range_index(self):
        self.assertEqual(fd.qt_filter(["Text|*.txt"], 7), "Text (*.txt)")


class Places(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.home = pathlib.Path(self.tmp.name) / "home"
        self.docs = self.home / "Documents"
        self.docs.mkdir(parents=True)
        self.space = self.home / ".local/share/raye/windows/notepad-"
        users = self.space / "drive_c/users/me"
        users.mkdir(parents=True)
        (self.space / "drive_c/Program Files/App").mkdir(parents=True)
        self.link = users / "Documents"
        self.link.symlink_to(self.docs)

    def tearDown(self):
        self.tmp.cleanup()

    def test_a_linked_folder_opens_where_it_really_is(self):
        self.assertEqual(fd.start_folder(str(self.link), str(self.home)), str(self.docs))

    def test_the_space_itself_is_never_shown(self):
        inside = self.space / "drive_c/Program Files/App"
        self.assertEqual(fd.start_folder(str(inside), str(self.home)), str(self.docs))

    def test_missing_folder_falls_back_on_documents(self):
        self.assertEqual(fd.start_folder("/nope/nothing", str(self.home)), str(self.docs))
        self.assertEqual(fd.start_folder("", str(self.home)), str(self.docs))

    def test_home_when_there_is_no_documents(self):
        self.docs.rename(self.home / "Docs2")
        self.assertEqual(fd.start_folder("", str(self.home)), str(self.home))

    def test_an_ordinary_folder_is_kept(self):
        other = self.home / "Music"
        other.mkdir()
        self.assertEqual(fd.start_folder(str(other), str(self.home)), str(other))


class Run(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        t = pathlib.Path(self.tmp.name)
        self.bin = t / "bin"
        self.bin.mkdir()
        k = self.bin / "kdialog"
        k.write_text(FAKE)
        k.chmod(k.stat().st_mode | stat.S_IEXEC)
        self.home = t / "home"
        (self.home / "Documents").mkdir(parents=True)
        self.args = t / "args"
        self.out = t / "out"
        self.env = dict(os.environ, PATH=f"{self.bin}:/usr/bin:/bin", HOME=str(self.home),
                        FAKE_ARGS=str(self.args), DISPLAY=":5", LANG="C")
        for key in ("WAYLAND_DISPLAY", "LANGUAGE", "LC_ALL", "LC_MESSAGES"):
            self.env.pop(key, None)

    def tearDown(self):
        self.tmp.cleanup()

    def run_tool(self, *args, out="", rc=0, env=None):
        e = dict(self.env, FAKE_OUT=out, FAKE_RC=str(rc))
        if env:
            e.update(env)
        return subprocess.run([sys.executable, "-I", str(TOOL), *args, "--out", str(self.out)],
                              env=e, capture_output=True, text=True, timeout=30)

    def called(self):
        return self.args.read_text().splitlines()

    def test_open(self):
        r = self.run_tool("--mode", "open", "--title", "Open", "--filter", "Text|*.txt", "--parent", "4194311",
                          out=str(self.home / "Documents/a.txt"))
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(self.out.read_text(encoding="utf-8"), str(self.home / "Documents/a.txt") + "\n")
        self.assertEqual(self.called(), ["--title", "Open", "--attach", "4194311", "--getopenfilename",
                                         str(self.home / "Documents"), "Text (*.txt)"])
        # the program's window is an X11 one: the dialog has to be too to sit on it
        self.assertEqual((self.args.parent / "args.platform").read_text().strip(), "xcb")

    def test_several_files(self):
        r = self.run_tool("--mode", "open-multiple", out="/x/a.txt\n/x/b c.txt")
        self.assertEqual(r.returncode, 0)
        self.assertIn("--multiple", self.called())
        self.assertIn("--separate-output", self.called())
        self.assertEqual(self.out.read_text().splitlines(), ["/x/a.txt", "/x/b c.txt"])

    def test_save_proposes_the_name(self):
        self.run_tool("--mode", "save", "--name", "report.docx", out="/x/report.docx")
        self.assertIn(str(self.home / "Documents" / "report.docx"), self.called())
        self.assertIn("--getsavefilename", self.called())

    def test_a_name_with_a_folder_is_cut_to_the_name(self):
        self.run_tool("--mode", "save", "--name", "..\\..\\evil.txt", out="/x/evil.txt")
        self.assertIn(str(self.home / "Documents" / "evil.txt"), self.called())

    def test_a_dialog_without_title_gets_the_windows_one(self):
        self.run_tool("--mode", "save", out="/x/a")
        self.assertEqual(self.called()[:2], ["--title", "Save As"])
        self.run_tool("--mode", "folder", out="/x/a", env={"LANG": "fr_CA.UTF-8"})
        self.assertEqual(self.called()[:2], ["--title", "Sélectionner un dossier"])

    def test_folder(self):
        self.run_tool("--mode", "folder", out="/x/dir")
        self.assertIn("--getexistingdirectory", self.called())

    def test_cancel(self):
        r = self.run_tool("--mode", "open", rc=1)
        self.assertEqual(r.returncode, 1)
        self.assertFalse(self.out.exists())

    def test_no_kdialog_lets_the_old_dialog_show(self):
        (self.bin / "kdialog").unlink()
        r = self.run_tool("--mode", "open")
        self.assertEqual(r.returncode, 3)

    def test_kdialog_crashing_lets_the_old_dialog_show(self):
        r = self.run_tool("--mode", "open", rc=134)
        self.assertEqual(r.returncode, 3)

    def test_no_screen(self):
        r = self.run_tool("--mode", "open", env={"DISPLAY": ""})
        self.assertEqual(r.returncode, 3)

    def test_empty_answer_is_a_cancel(self):
        r = self.run_tool("--mode", "open", out="")
        self.assertEqual(r.returncode, 1)

    def test_dialog_closes_when_the_program_ends(self):
        pidfile = self.args.parent / "kdialog.pid"
        (self.bin / "kdialog").write_text(f"#!/bin/sh\necho $$ > {pidfile}\nexec sleep 60\n")
        # the helper's parent is a shell that ends after one second
        subprocess.run(["sh", "-c", f"{sys.executable} -I {TOOL} --mode open --out {self.out} & sleep 1"],
                       env=self.env, timeout=10)
        pid = int(pidfile.read_text())
        for _ in range(50):
            if not os.path.exists(f"/proc/{pid}"):
                break
            subprocess.run(["sleep", "0.1"])
        self.assertFalse(os.path.exists(f"/proc/{pid}"))
        self.assertFalse(self.out.exists())

    def test_bad_mode(self):
        r = self.run_tool("--mode", "delete")
        self.assertEqual(r.returncode, 2)

    def test_without_a_window_the_session_platform_is_kept(self):
        self.run_tool("--mode", "open", out="/x/a", env={"WAYLAND_DISPLAY": "wayland-0"})
        self.assertEqual((self.args.parent / "args.platform").read_text().strip(), "")


class Patch(unittest.TestCase):
    """The comdlg32 side: what the layer hands over and when it keeps its own dialog."""

    def setUp(self):
        self.text = PATCH.read_text()

    def test_in_the_series(self):
        series = (ROOT / "patches" / "base" / "series").read_text().split()
        self.assertIn(PATCH.name, series)

    def test_every_entry_point(self):
        for where in ("GetFileDialog95", "IFileDialog2_fnShow"):
            self.assertIn(where, self.text)

    def test_helper_path_and_switch(self):
        self.assertIn("/opt/raye-wine/bin/raye-file-dialog", self.text)
        self.assertIn("RAYE_FILE_DIALOG", self.text)

    def test_own_templates_keep_the_old_dialog(self):
        self.assertIn("OFN_ENABLETEMPLATE", self.text)

    def test_helper_is_installed(self):
        rules = (ROOT / "debian" / "rules").read_text()
        self.assertIn("release/windows/raye-file-dialog", rules)


if __name__ == "__main__":
    unittest.main()
