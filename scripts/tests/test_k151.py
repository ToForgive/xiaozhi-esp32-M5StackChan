"""K151 behavioral acceptance tests on the host; these do not assert hardware validation."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
BOARD = ROOT / "main/boards/m5stack/stackchan-k151"

class K151Tests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("g++"), "host C++ compiler required")
    def test_state_recovery_and_ir(self):
        with tempfile.TemporaryDirectory() as folder:
            binary = Path(folder) / "k151-test"
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror",
                            "-I", str(BOARD), "-I", str(ROOT / "main"),
                            str(ROOT / "scripts/tests/k151/model_test.cc"), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


    @unittest.skipUnless(shutil.which("g++"), "host C++ compiler required")
    def test_disabled_ota_returns_before_touching_display_or_audio(self):
        # Compile the real entry guard, replacing only platform objects. A disabled
        # update must leave the conversation running instead of entering teardown.
        source = (ROOT / "main/application.cc").read_text()
        entry = source.split("bool Application::UpgradeFirmware(", 1)[1]
        prefix = "bool Application::UpgradeFirmware(" + entry.split("    auto display = board.GetDisplay();", 1)[0]
        program = r"""
#include <cassert>
#include <string>
#define ESP_LOGW(...) ((void)0)
struct Board {
    bool allowed = false, display_touched = false;
    static Board& GetInstance() { static Board board; return board; }
    bool IsFirmwareUpdateAllowed() const { return allowed; }
    void* GetDisplay() { display_touched = true; return nullptr; }
};
struct Application { bool UpgradeFirmware(const std::string&, const std::string&); };
""" + prefix + r"""
    (void)url; (void)version;
    (void)board.GetDisplay();
    return true;
}
int main() {
    Application app; auto& board = Board::GetInstance();
    assert(!app.UpgradeFirmware("https://example.invalid/fw", ""));
    assert(!board.display_touched);
    board.allowed = true;
    assert(app.UpgradeFirmware("https://example.invalid/fw", ""));
    assert(board.display_touched);
}
"""
        with tempfile.TemporaryDirectory() as folder:
            src = Path(folder) / "ota_entry.cc"
            src.write_text(program)
            binary = Path(folder) / "ota_entry"
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(src), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    @unittest.skipUnless(shutil.which("g++"), "host C++ compiler required")
    def test_ota_header_preserves_fragmented_http_reads(self):
        source = (ROOT / "main/ota.cc").read_text()
        statement = next(line.strip() for line in source.splitlines() if "image_header.append(" in line)
        program = r"""
#include <cassert>
#include <cstring>
#include <string>
int main() {
    const std::string payload = "abcdefghijklmnopqrstuvwxyz0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    for (int fragment : {1, 2, 7, 16, 62}) {
        char buffer[128]; size_t buffer_offset = 0;
        std::string image_header;
        while (buffer_offset < payload.size()) {
            int n = static_cast<int>(payload.size() - buffer_offset);
            if (n > fragment) n = fragment;
            memcpy(buffer + buffer_offset, payload.data() + buffer_offset, n);
            buffer_offset += n;
""" + statement + r"""
        }
        assert(image_header == payload);
    }
}
"""
        with tempfile.TemporaryDirectory() as folder:
            src = Path(folder) / "ota_header.cc"
            src.write_text(program)
            binary = Path(folder) / "ota_header"
            subprocess.run(["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(src), "-o", str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_partition_preserves_calibration_address_and_fits_flash(self):
        rows = []
        for line in (ROOT / "partitions/k151/16m.csv").read_text().splitlines():
            if line.strip() and not line.startswith("#"):
                rows.append([cell.strip() for cell in line.split(",")])
        def size(value):
            return int(value[:-1]) * 1024 * 1024 if value.endswith("M") else int(value, 0)
        end = 0
        partitions = {}
        for name, kind, subtype, offset, length, *_ in rows:
            start = int(offset, 0) if offset else end
            self.assertGreaterEqual(start, end)
            end = start + size(length)
            partitions[name] = (start, size(length))
        self.assertEqual(partitions["nvs"], (0x9000, 0x4000))
        self.assertEqual(partitions["ota_0"][1], partitions["ota_1"][1])
        self.assertEqual(partitions["assets"], (0xA00000, 4*1024*1024))
        self.assertLessEqual(end, 16*1024*1024)

    def test_canonical_variant_identity(self):
        # Exercise the canonical discovery chain, not an independent hand-written config parser.
        import importlib.util
        spec = importlib.util.spec_from_file_location("k151_build", ROOT/"scripts/build.py")
        build = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(build)
        matches = [v for v in build._collect_variants(idf_version=(6,1,0))
                   if v["board"] == "m5stack/stackchan-k151"]
        self.assertEqual(len(matches), 1)
        self.assertEqual(matches[0]["type"], "m5stack-stackchan-k151")
        self.assertEqual(matches[0]["config"], "CONFIG_BOARD_TYPE_M5STACK_STACKCHAN_K151")
        self.assertEqual(matches[0]["target"], "esp32s3")
