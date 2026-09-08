"""The sampled bundle must never turn partial/excluded results into acceptance."""
import copy
import importlib
import unittest

BUNDLE = importlib.import_module("build-sdk-bundle")


class SampleGateTests(unittest.TestCase):
    def test_accept_only_four_all_pass_samples(self):
        report = dict(complete=False, clean_cycles=4,
                      render_targets={str(i): {} for i in range(4)},
                      configurations={str(i): dict(executed=51, counts={"Pass": 51})
                                      for i in range(4)})
        BUNDLE.require_sample(report)
        mutations = [
            lambda r: r.update(complete=True),
            lambda r: r.update(clean_cycles=3),
            lambda r: r["configurations"].pop("3"),
            lambda r: r["render_targets"].pop("3"),
            lambda r: r["configurations"]["0"].update(executed=50),
            lambda r: r["configurations"]["0"].update(counts={"Pass": 50, "NotSupported": 1}),
            lambda r: r["configurations"]["0"].update(counts={"Pass": 50, "Fail": 1}),
        ]
        for mutate in mutations:
            bad = copy.deepcopy(report)
            mutate(bad)
            with self.assertRaises(ValueError):
                BUNDLE.require_sample(bad)


if __name__ == "__main__":
    unittest.main()
