# SPDX-License-Identifier: LGPL-2.1-or-later
"""Cameras, the way Windows programs look for them:

    python3 -I -B -m unittest discover -s tests

- Media Foundation (base/0024): MFEnumDeviceSources(VIDCAP) always said 0
  cameras and MFCreateDeviceSource was a stub. The cameras now come from
  GStreamer (v4l2src), with the formats the camera gives;
- DirectShow (base/0025): 32-bit programs got a media type with a garbage
  pUnk, and GetStreamCaps read one entry past the list;
- the probe the cameras are checked with stays buildable.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parent.parent
BASE = ROOT / "patches" / "base"
SOURCE = BASE / "0024-camera-media-source.patch"
QCAP = BASE / "0025-qcap-capture-fixes.patch"
SAMPLES = ROOT / "tests" / "samples"


def series():
    return [l.strip() for l in (BASE / "series").read_text().splitlines()
            if l.strip() and not l.startswith("#")]


def files(patch):
    """{path: (removed lines, added lines)} of a patch."""
    out, current = {}, None
    for line in patch.read_text().splitlines():
        if line.startswith("diff --git "):
            current = line.split(" b/", 1)[1]
            out[current] = ([], [])
        elif current is None or line.startswith(("---", "+++")):
            continue
        elif line.startswith("-"):
            out[current][0].append(line[1:])
        elif line.startswith("+"):
            out[current][1].append(line[1:])
    return out


def added(patch, path):
    return "\n".join(files(patch)[path][1])


class MediaFoundation(unittest.TestCase):
    def test_patches_come_after_the_serial_ports(self):
        names = series()
        self.assertIn(SOURCE.name, names)
        self.assertIn(QCAP.name, names)
        self.assertLess(names.index("0023-serial-ports.patch"), names.index(SOURCE.name))
        self.assertLess(names.index(SOURCE.name), names.index(QCAP.name))

    def test_the_stub_is_gone(self):
        removed, add = files(SOURCE)["dlls/mf/main.c"]
        self.assertIn('FIXME("Not implemented for video capture devices.\\n");', "\n".join(removed))
        text = "\n".join(add)
        self.assertIn("winegstreamer_enum_video_capture", text)
        self.assertIn("winegstreamer_create_video_capture_source", text)
        self.assertIn("MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK", text)

    def test_mf_loads_gstreamer_only_when_asked(self):
        # mf.dll is loaded by every player; a camera list must not pull GStreamer in
        _, add = files(SOURCE)["dlls/mf/Makefile.in"]
        self.assertTrue(any(l.startswith("DELAYIMPORTS") and "winegstreamer" in l for l in add))

    def test_exports(self):
        spec = added(SOURCE, "dlls/winegstreamer/winegstreamer.spec")
        for name in ("winegstreamer_enum_video_capture", "winegstreamer_create_video_capture_source"):
            self.assertIn(name, spec)

    def test_cameras_come_from_v4l2_and_keep_their_formats(self):
        unix = added(SOURCE, "dlls/winegstreamer/wg_camera.c")
        self.assertIn('"v4l2deviceprovider"', unix)
        self.assertIn('"v4l2src"', unix)
        for fmt in ('"YUY2"', '"NV12"', '"image/jpeg"'):
            self.assertIn(fmt, unix)
        # a camera that only gives MJPG at a size also gets a decoded NV12 there
        self.assertIn('"jpegdec"', unix)
        for guid in ("MFVideoFormat_NV12", "MFVideoFormat_YUY2", "MFVideoFormat_MJPG"):
            self.assertIn(guid, unix)
        # MJPG frames vary in size, the others are fixed
        pe = added(SOURCE, "dlls/winegstreamer/video_capture.c")
        self.assertIn("MF_MT_COMPRESSED", pe)
        self.assertIn("MF_MT_DEFAULT_STRIDE", pe)

    def test_a_live_source_with_the_events_a_reader_waits_for(self):
        pe = added(SOURCE, "dlls/winegstreamer/video_capture.c")
        for event in ("MENewStream", "MESourceStarted", "MEStreamStarted", "MEMediaSample",
                      "MESourceStopped", "MEStreamStopped"):
            self.assertIn(event, pe)
        self.assertIn("MFMEDIASOURCE_IS_LIVE", pe)

    def test_every_unix_call_has_a_32_bit_entry(self):
        tables = added(SOURCE, "dlls/winegstreamer/wg_parser.c")
        calls = re.findall(r"X\((wg_camera_\w+)\)", tables)
        names = set(calls)
        self.assertGreaterEqual(len(names), 7)
        for name in names:
            self.assertEqual(calls.count(name), 2, name)

    def test_unix_parameters_have_no_pointers(self):
        # the 32-bit table uses the same functions, so the layout must not move
        header = added(SOURCE, "dlls/winegstreamer/unixlib.h")
        blocks = re.findall(r"struct wg_camera_\w+\n\{(.*?)\n\};", header, re.S)
        self.assertGreaterEqual(len(blocks), 5)
        for block in blocks:
            self.assertNotIn("*", re.sub(r"/\*.*?\*/", "", block, flags=re.S))

    def test_friendly_names_never_show_the_layer(self):
        for path, (_, add) in files(SOURCE).items():
            for line in add:
                for text in re.findall(r'L?"([^"]*)"', line):
                    self.assertIsNone(re.search(r"\bWine\b", text), (path, line))

    def test_the_full_build_watches_camera_names(self):
        self.assertIn("dlls/winegstreamer/wg_camera.c", (ROOT / "tools" / "check-names.sh").read_text())

    def test_symbolic_links_look_like_windows_ones(self):
        unix = added(SOURCE, "dlls/winegstreamer/wg_camera.c")
        self.assertIn("vid_%", unix)
        self.assertIn("{e5323777-f976-4f5b-9b55-b94699c46e44}", unix)


class DirectShow(unittest.TestCase):
    def test_32_bit_media_types_have_no_garbage_pointer(self):
        text = added(QCAP, "dlls/qcap/v4l.c")
        self.assertIn("mt32->pUnk", text)

    def test_stream_caps_stop_at_the_count(self):
        removed, add = files(QCAP)["dlls/qcap/vfwcapture.c"]
        self.assertIn("if (index > count)", "\n".join(removed))
        self.assertIn("if (index < 0 || index >= count)", "\n".join(add))


class Probe(unittest.TestCase):
    def test_probe_is_built_64_and_32_bit(self):
        makefile = (SAMPLES / "Makefile").read_text()
        self.assertTrue((SAMPLES / "cam-check.c").is_file())
        for target in ("cam-check.exe:", "cam-check32.exe:"):
            self.assertIn(target, makefile)

    def test_probe_reads_both_apis(self):
        text = (SAMPLES / "cam-check.c").read_text()
        for call in ("MFEnumDeviceSources", "MFCreateSourceReaderFromMediaSource", "ReadSample",
                     "CLSID_VideoInputDeviceCategory"):
            self.assertIn(call, text)


if __name__ == "__main__":
    unittest.main()
