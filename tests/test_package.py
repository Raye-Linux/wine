# SPDX-License-Identifier: LGPL-2.1-or-later
"""release/windows/raye-windows-package: what a double-click on a Store
package (.msix, .appx and their bundles) leads to.

    python3 -I -B -m unittest discover -s tests

Built on small packages made here with zipfile, the way the Store makes
them: a manifest, percent-encoded names, VFS folders, bundles of packages.
Nothing runs a Windows program; the step that hands the program to
raye-run-windows is checked by the command it would run.
"""
import importlib.machinery
import importlib.util
import io
import os
import pathlib
import re
import sys
import tempfile
import unittest
import zipfile

ROOT = pathlib.Path(__file__).resolve().parent.parent
TOOL = ROOT / "release" / "windows" / "raye-windows-package"
sys.dont_write_bytecode = True


def load():
    loader = importlib.machinery.SourceFileLoader("raye_windows_package", str(TOOL))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    return mod


pkg = load()

MANIFEST = """<?xml version="1.0" encoding="utf-8"?>
<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10"
  xmlns:uap="http://schemas.microsoft.com/appx/manifest/uap/windows10"
  xmlns:uap10="http://schemas.microsoft.com/appx/manifest/uap/windows10/10"
  xmlns:rescap="http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities">
  <Identity Name="{name}" Publisher="CN=Someone" Version="1.2.3.0" ProcessorArchitecture="{arch}"/>
  <Properties><DisplayName>{display}</DisplayName><Logo>Assets\\StoreLogo.png</Logo></Properties>
  <Dependencies>{deps}</Dependencies>
  <Applications>{apps}</Applications>
</Package>
"""
WIN32_APP = ('<Application Id="App" Executable="{exe}" EntryPoint="Windows.FullTrustApplication">'
             '<uap:VisualElements DisplayName="{display}" Square44x44Logo="Assets\\Square44x44Logo.png"'
             ' Square150x150Logo="Assets\\Square150.png" Description="d" BackgroundColor="transparent"/>'
             '</Application>')
UWP_APP = '<Application Id="App" Executable="App.exe" EntryPoint="App.App"/>'
PNG = b"\x89PNG\r\n\x1a\n" + b"\0" * 32


def make(path, name="Acme.Tool", display="Acme Tool", arch="x64", apps=None, deps="",
         files=None):
    apps = WIN32_APP.format(exe="Tool\\Tool.exe", display=display) if apps is None else apps
    files = {"Tool/Tool.exe": b"MZ" + b"\0" * 64, "Assets/Square44x44Logo.scale-200.png": PNG,
             "Assets/Square44x44Logo.targetsize-48.png": PNG,
             "VFS/SystemX64/helper.dll": b"MZ", "AppxSignature.p7x": b"sig",
             "AppxBlockMap.xml": b"<x/>", "[Content_Types].xml": b"<x/>"} if files is None else files
    with zipfile.ZipFile(path, "w") as z:
        z.writestr("AppxManifest.xml", MANIFEST.format(name=name, display=display, arch=arch,
                                                       apps=apps, deps=deps))
        for n, data in files.items():
            z.writestr(n.replace(" ", "%20"), data)
    return path


def bundle(path, inner):
    """inner: {file name: (arch, bytes of a package)}"""
    entries = "".join('<Package Type="application" Architecture="%s" FileName="%s"/>' % (a, n)
                      for n, (a, _) in inner.items())
    with zipfile.ZipFile(path, "w") as z:
        z.writestr("AppxMetadata/AppxBundleManifest.xml",
                   '<Bundle xmlns="http://schemas.microsoft.com/appx/2013/bundle">'
                   '<Packages>%s</Packages></Bundle>' % entries)
        for n, (_, data) in inner.items():
            z.writestr(n, data, compress_type=zipfile.ZIP_STORED)
    return path


class Inspect(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = pathlib.Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_a_desktop_program_in_a_package_can_run(self):
        info = pkg.inspect(make(self.dir / "a.msix"))
        self.assertEqual(info["kind"], "win32")
        self.assertEqual(info["name"], "Acme.Tool")
        self.assertEqual(info["display"], "Acme Tool")
        self.assertEqual(info["executable"], "Tool/Tool.exe")

    def test_packaged_classic_app_counts_as_desktop(self):
        apps = ('<Application Id="A" Executable="T.exe" uap10:RuntimeBehavior="packagedClassicApp"'
                ' uap10:TrustLevel="mediumIL"/>')
        self.assertEqual(pkg.inspect(make(self.dir / "a.msix", apps=apps))["kind"], "win32")

    def test_a_store_app_cannot(self):
        self.assertEqual(pkg.inspect(make(self.dir / "a.appx", apps=UWP_APP))["kind"], "uwp")

    def test_one_store_part_makes_it_a_store_app(self):
        apps = WIN32_APP.format(exe="Tool.exe", display="x") + UWP_APP
        self.assertEqual(pkg.inspect(make(self.dir / "a.msix", apps=apps))["kind"], "uwp")

    def test_the_new_windows_ui_runtime_cannot(self):
        deps = '<PackageDependency Name="Microsoft.WindowsAppRuntime.1.6" MinVersion="1.0.0.0"/>'
        self.assertEqual(pkg.inspect(make(self.dir / "a.msix", deps=deps))["kind"], "winui")

    def test_compiled_xaml_means_the_windows_ui_framework(self):
        files = {"Tool/Tool.exe": b"MZ", "Mile.Xaml.Styles.SunValley.xbf": b"x"}
        self.assertEqual(pkg.inspect(make(self.dir / "a.msix", files=files))["kind"], "winui")

    def test_the_cpp_runtime_dependency_is_fine(self):
        deps = '<PackageDependency Name="Microsoft.VCLibs.140.00.UWPDesktop" MinVersion="14.0"/>'
        self.assertEqual(pkg.inspect(make(self.dir / "a.msix", deps=deps))["kind"], "win32")

    def test_arm_only_cannot(self):
        self.assertEqual(pkg.inspect(make(self.dir / "a.msix", arch="arm64"))["kind"], "arm")

    def test_a_resource_name_falls_back_to_the_identity(self):
        info = pkg.inspect(make(self.dir / "a.msix", display="ms-resource:AppName"))
        self.assertEqual(info["display"], "Acme Tool")
        info = pkg.inspect(make(self.dir / "b.msix", name="Microsoft.WindowsTerminal",
                                display="ms-resource:AppName"))
        self.assertEqual(info["display"], "Microsoft Windows Terminal")

    def test_not_a_package(self):
        bad = self.dir / "a.msix"
        bad.write_bytes(b"not a zip")
        self.assertEqual(pkg.inspect(bad)["kind"], "bad")
        with zipfile.ZipFile(self.dir / "b.msix", "w") as z:
            z.writestr("readme.txt", "x")
        self.assertEqual(pkg.inspect(self.dir / "b.msix")["kind"], "bad")

    def test_a_bundle_gives_its_x64_package(self):
        x64 = make(self.dir / "x64.msix", display="Sixty Four").read_bytes()
        x86 = make(self.dir / "x86.msix", arch="x86", display="Thirty Two").read_bytes()
        arm = make(self.dir / "arm.msix", arch="arm64", display="Arm").read_bytes()
        b = bundle(self.dir / "a.msixbundle", {"arm.msix": ("arm64", arm), "x86.msix": ("x86", x86),
                                               "x64.msix": ("x64", x64)})
        info = pkg.inspect(b)
        self.assertEqual(info["kind"], "win32")
        self.assertEqual(info["display"], "Sixty Four")
        self.assertEqual(info["inner"], "x64.msix")

    def test_a_bundle_falls_back_to_x86(self):
        x86 = make(self.dir / "x86.msix", arch="x86").read_bytes()
        info = pkg.inspect(bundle(self.dir / "a.appxbundle", {"x86.appx": ("x86", x86)}))
        self.assertEqual((info["kind"], info["arch"]), ("win32", "x86"))


class Extract(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = pathlib.Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_files_land_with_decoded_names_and_no_package_bookkeeping(self):
        files = {"Tool/Tool.exe": b"MZ", "Program Files/data.txt": b"x",
                 "AppxSignature.p7x": b"s", "AppxBlockMap.xml": b"b", "[Content_Types].xml": b"c"}
        src = make(self.dir / "a.msix", files=files)
        dest = self.dir / "out"
        pkg.extract(src, pkg.inspect(src), dest)
        self.assertTrue((dest / "Program Files" / "data.txt").is_file())
        self.assertTrue((dest / "Tool" / "Tool.exe").is_file())
        for n in ("AppxSignature.p7x", "AppxBlockMap.xml", "[Content_Types].xml"):
            self.assertFalse((dest / n).exists(), n)

    def test_system_dlls_of_the_package_sit_next_to_the_program(self):
        src = make(self.dir / "a.msix")
        dest = self.dir / "out"
        pkg.extract(src, pkg.inspect(src), dest)
        self.assertTrue((dest / "Tool" / "helper.dll").is_file())

    def test_a_name_that_climbs_out_is_refused(self):
        src = make(self.dir / "a.msix", files={"Tool/Tool.exe": b"MZ", "../evil.exe": b"MZ",
                                               "/abs.exe": b"MZ", "a/..%2F..%2Fx.exe": b"MZ"})
        dest = self.dir / "out"
        with self.assertRaises(ValueError):
            pkg.extract(src, pkg.inspect(src), dest)
        self.assertFalse((self.dir / "evil.exe").exists())

    def test_a_bundle_extracts_its_chosen_package(self):
        x64 = make(self.dir / "x64.msix").read_bytes()
        b = bundle(self.dir / "a.msixbundle", {"x64.msix": ("x64", x64)})
        dest = self.dir / "out"
        pkg.extract(b, pkg.inspect(b), dest)
        self.assertTrue((dest / "Tool" / "Tool.exe").is_file())

    def test_the_icon_is_the_biggest_of_its_scales(self):
        src = make(self.dir / "a.msix")
        dest = self.dir / "out"
        pkg.extract(src, pkg.inspect(src), dest)
        icon = pkg.icon(dest, pkg.inspect(src))
        self.assertEqual(icon.name, "Square44x44Logo.scale-200.png")

    def test_the_icon_skips_contrast_forms_and_huge_sizes(self):
        names = ["Square44x44Logo.scale-200_contrast-black.png", "Square44x44Logo.targetsize-1024.png",
                 "Square44x44Logo.targetsize-256_altform-lightunplated.png",
                 "Square44x44Logo.targetsize-256.png", "Square44x44Logo.targetsize-256_altform-unplated.png",
                 "Square44x44Logo.targetsize-48_altform-unplated.png"]
        files = {"Tool/Tool.exe": b"MZ"}
        files.update({"Assets/" + n: PNG for n in names})
        src = make(self.dir / "a.msix", files=files)
        dest = self.dir / "out"
        pkg.extract(src, pkg.inspect(src), dest)
        self.assertEqual(pkg.icon(dest, pkg.inspect(src)).name,
                         "Square44x44Logo.targetsize-256_altform-unplated.png")


class Launch(unittest.TestCase):
    def test_the_start_entry_runs_the_program_and_shows_its_icon(self):
        text = pkg.desktop_entry("Acme Tool", "/home/a b/x/Tool/Tool.exe", "/home/a b/x/i.png")
        self.assertIn("Name=Acme Tool\n", text)
        self.assertIn('Exec=raye-run-windows "/home/a b/x/Tool/Tool.exe"\n', text)
        self.assertIn("Icon=/home/a b/x/i.png\n", text)
        self.assertIn("StartupWMClass=tool.exe\n", text)

    def test_quotes_and_dollars_are_escaped(self):
        text = pkg.desktop_entry("X", '/h/a"$b/T.exe', "")
        self.assertIn(r'Exec=raye-run-windows "/h/a\"\$b/T.exe"', text)
        self.assertIn("Icon=raye-eye\n", text)

    def test_folder_name_is_safe(self):
        self.assertEqual(pkg.slug("Acme.Tool"), "acme-tool")
        self.assertEqual(pkg.slug("../.."), "package")


class Words(unittest.TestCase):
    def test_every_message_in_english_and_french(self):
        for key, pair in pkg.TEXT.items():
            self.assertEqual(set(pair), {"en", "fr"}, key)
            self.assertNotEqual(pair["en"], pair["fr"], key)

    def test_never_the_layer_name(self):
        import re
        for pair in pkg.TEXT.values():
            for s in pair.values():
                self.assertIsNone(re.search(r"\bWine\b", s), s)

    def test_known_store_apps_point_somewhere_real(self):
        for name, alt in pkg.ALTERNATIVES.items():
            self.assertTrue(alt.get("store") or alt.get("web"), name)
            if alt.get("web"):
                self.assertTrue(alt["web"].startswith("https://"), name)


class Packaging(unittest.TestCase):
    def test_the_package_ships_the_tool_and_its_file_types(self):
        rules = (ROOT / "debian" / "rules").read_text()
        self.assertIn("usr/bin/raye-windows-package", rules)
        self.assertIn("usr/share/applications/org.raye.windows.package.desktop", rules)
        self.assertIn("usr/share/applications", (ROOT / "debian" / "raye-wine.install").read_text().split())
        desktop = (ROOT / "release" / "windows" / "org.raye.windows.package.desktop").read_text()
        types = set(re.search(r"(?m)^MimeType=(.*)$", desktop).group(1).strip(";").split(";"))
        self.assertEqual(types, {"application/msix", "application/appx",
                                 "application/msixbundle", "application/appxbundle"})
        self.assertIn("Name[fr]=", desktop)
        self.assertTrue(os.access(TOOL, os.X_OK))


if __name__ == "__main__":
    unittest.main()
