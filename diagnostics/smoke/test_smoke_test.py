from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path

import smoke_test


def write_ppm(path: Path, width: int, height: int, pixels: bytes) -> None:
    path.write_bytes(f"P6\n{width} {height}\n255\n".encode("ascii") + pixels)


class SmokeTestTests(unittest.TestCase):
    def test_exact_images_match(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            pixels = bytes([0, 10, 20, 30, 40, 50])
            write_ppm(root / "baseline.ppm", 2, 1, pixels)
            write_ppm(root / "candidate.ppm", 2, 1, pixels)

            result = smoke_test.compare_images(
                root / "baseline.ppm",
                root / "candidate.ppm",
                root / "diff.ppm",
            )

            self.assertTrue(result.exact)
            self.assertEqual(result.changed_pixels, 0)
            self.assertEqual(smoke_test.read_ppm(root / "diff.ppm").pixels, bytes(6))

    def test_changed_pixels_and_channel_metrics_are_reported(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_ppm(root / "baseline.ppm", 2, 1, bytes([0, 10, 20, 30, 40, 50]))
            write_ppm(root / "candidate.ppm", 2, 1, bytes([10, 10, 15, 30, 40, 50]))

            result = smoke_test.compare_images(
                root / "baseline.ppm",
                root / "candidate.ppm",
                root / "diff.ppm",
            )

            self.assertFalse(result.exact)
            self.assertEqual(result.changed_pixels, 1)
            self.assertEqual(result.total_pixels, 2)
            self.assertEqual(result.maximum_channel_delta, 10)
            self.assertEqual(result.mean_absolute_channel_delta, 2.5)
            self.assertEqual(
                smoke_test.read_ppm(root / "diff.ppm").pixels,
                bytes([10, 0, 5, 0, 0, 0]),
            )

    def test_capture_paths_are_rewritten_without_changing_the_source(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source.json"
            destination = root / "generated" / "config.json"
            capture_directory = root / "captures"
            source.write_text(
                json.dumps(
                    {
                        "captures": [
                            {"frame": 1, "path": "early.ppm"},
                            {"frame": 2, "path": "nested/settled.ppm"},
                        ]
                    }
                ),
                encoding="utf-8",
            )

            filenames = smoke_test.prepare_config(source, destination, capture_directory)

            self.assertEqual(filenames, ["early.ppm", "settled.ppm"])
            generated = json.loads(destination.read_text(encoding="utf-8"))
            self.assertEqual(
                [capture["path"] for capture in generated["captures"]],
                [
                    str((capture_directory / "early.ppm").resolve()),
                    str((capture_directory / "settled.ppm").resolve()),
                ],
            )
            self.assertEqual(json.loads(source.read_text(encoding="utf-8"))["captures"][0]["path"], "early.ppm")

    def test_suite_fingerprint_tracks_semantic_config_changes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            config = root / "config.json"
            suite = root / "suite.json"
            config.write_text('{"captures":[{"frame":1,"path":"frame.ppm"}]}', encoding="utf-8")
            suite.write_text(
                '{"version":1,"cases":[{"name":"case","target":"target","config":"config.json"}]}',
                encoding="utf-8",
            )
            cases = smoke_test.load_suite(suite)
            original = smoke_test.suite_fingerprint(suite, cases)

            config.write_text(
                json.dumps({"captures": [{"frame": 1, "path": "frame.ppm"}]}, indent=2),
                encoding="utf-8",
            )
            self.assertEqual(smoke_test.suite_fingerprint(suite, cases), original)

            config.write_text(
                json.dumps({"captures": [{"frame": 2, "path": "frame.ppm"}]}),
                encoding="utf-8",
            )
            self.assertNotEqual(smoke_test.suite_fingerprint(suite, cases), original)


if __name__ == "__main__":
    unittest.main()
