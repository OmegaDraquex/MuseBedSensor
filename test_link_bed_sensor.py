# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from pathlib import Path
import os
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
# CI supplies pinned upstream sources; local IDF builds already have cJSON.
JSON = Path(os.environ.get(
    "CJSON_SOURCE_DIR", ROOT / "managed_components/espressif__cjson/cJSON"
))


class LinkBedSensorTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        out = Path(cls.temp.name)
        source = (ROOT / "main/bed_sensor.c").read_text()
        start = source.index("// ---- State machine (host-tested)")
        (out / "bed_state.inc").write_text(source[start:source.index("// ---- Sampler", start)])
        cc = shlex.split(os.environ.get("CC", "cc"))
        flags = ["-Wall", "-Wextra", "-Werror", "-DCJSON_NESTING_LIMIT=16", "-I", str(JSON), "-I", str(out)]
        commands = [
            [*cc, "-std=c11", *flags, "-c", str(JSON / "cJSON.c"), "-o", str(out / "cjson.o")],
            [*cc, "-std=c11", *flags, str(ROOT / "tests/link_bed_sensor_harness.c"), str(out / "cjson.o"), "-o", str(out / "bed")],
        ]
        for cmd in commands:
            compiled = subprocess.run(cmd, capture_output=True, text=True)
            if compiled.returncode:
                raise AssertionError(compiled.stdout + compiled.stderr)
        cls.out = out

    def test_dwell_and_result(self):
        result = subprocess.run([str(self.out / "bed")], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("bed harness ok", result.stdout)
        self.assertIn('"occupied":true', result.stdout.replace(" ", ""))

    def test_out_dwell_is_sixty_seconds(self):
        source = (ROOT / "main/bed_sensor.c").read_text()
        self.assertIn("#define BED_OUT_DWELL_MS 60000", source)

    def test_command_is_advertised_and_dispatched_with_the_feature(self):
        noise = (ROOT / "main/noise_control.cpp").read_text()
        start = noise.index("#if CONFIG_HOMEHUB_BED_SENSOR")
        self.assertIn('add_command(commands, "bed.occupancy"', noise[start:noise.index("#endif", start)])
        app = (ROOT / "main/app.c").read_text()
        start = app.index("static cJSON *on_ws_command(")
        dispatch = app[start:app.index("unsupported command", start)]
        block = dispatch[dispatch.index("#if CONFIG_HOMEHUB_BED_SENSOR"):]
        self.assertIn('"bed.occupancy"', block[:block.index("#endif")])
        self.assertIn("bed_sensor_init();", app)

    def test_console_sets_threshold_and_reports_status(self):
        console = (ROOT / "main/sdk_token_console.c").read_text()
        self.assertIn("bed_sensor_console_command", console)
        source = (ROOT / "main/bed_sensor.c").read_text()
        self.assertIn('"bed_threshold="', source)
        self.assertIn('"bed_status"', source)
        self.assertIn("@bed_threshold ok", source)

    def test_supermini_enables_the_sensor_on_gpio4(self):
        overlay = (ROOT / "devices/sdkconfig.c3-supermini").read_text()
        self.assertIn("CONFIG_HOMEHUB_BED_SENSOR=y", overlay)
        kconfig = (ROOT / "main/Kconfig.projbuild").read_text()
        gpio = kconfig[kconfig.index("config HOMEHUB_BED_ADC_GPIO"):]
        gpio = gpio[: gpio.index("config HOMEHUB_BED_THRESHOLD")]
        self.assertIn("depends on HOMEHUB_BED_SENSOR", gpio)
        self.assertIn("default 4", gpio)
        threshold = kconfig[kconfig.index("config HOMEHUB_BED_THRESHOLD"):]
        self.assertIn("default 2900", threshold[: threshold.index("config HOMEHUB_DISPLAY_COMMANDS")])


if __name__ == "__main__":
    unittest.main()
