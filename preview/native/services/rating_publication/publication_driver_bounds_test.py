#!/usr/bin/env python3
"""Explicit native CLI policy cases, independent of source equality credit."""
import argparse
import hashlib
import json
import pathlib

from publication_fixture_test import run_cases, sha


def state(op, *, q="-", r="-", k="-", w=0, d=0, run="", tim="", call="", rng=0, clock=0, trace="-"):
    # Only formatting: each caller supplies the complete expected state. This
    # helper neither advances a queue nor computes a production transition.
    return (f"{op} | Q={q} R={r} K={k} W={w} D={d} RUN={run} TIM={tim} CALL={call} "
            f"RNG={rng} CLOCK={clock} TRACE={trace}")


def packaged(name, args, rows):
    return (name, args, ("\n".join(command for command, _ in rows) + "\n").encode(),
            ("\n".join(expected for _, expected in rows) + "\n").encode())


def plan():
    rows = [("Z", state("Z"))]
    malformed = ["", "UNKNOWN", "Z extra", "D A", "D A 2", "D lowercase 0", "D " + "A" * 65 + " 0",
                 "CACHE MISSING", "ADD MISSING 1", "RNG 1,", "RNG +1", "TIME 9223372036854775808",
                 "TIME -9223372036854775809", "TIME 1,,2", "REMOVED A -1", "STOPFLAG 2", "STEP extra",
                 "DONE 0 X", "DONE -1 N", "DONE 18446744073709551616 N", "FIRE -1", "MODE UNKNOWN",
                 "FAULT NONE", "BEFOREWRITE NONE", "D N NULL", "SNAP extra", "CLOSE extra"]
    rows.extend((command, state("ERR INPUT")) for command in malformed)
    rows.extend([("D A 0", state("D")), ("D A 0", state("ERR INPUT")), ("D B 0", state("ERR LIMIT")),
                 ("CACHE A", state("CACHE", r="A", k="A")),
                 ("CACHE MISSING", state("ERR INPUT", r="A", k="A")),
                 ("RNG " + ",".join("0" for _ in range(1024)), state("RNG", r="A", k="A")),
                 ("RNG " + ",".join("0" for _ in range(1025)), state("ERR LIMIT", r="A", k="A")),
                 ("TIME " + ",".join("0" for _ in range(1024)), state("TIME", r="A", k="A")),
                 ("TIME " + ",".join("0" for _ in range(1025)), state("ERR LIMIT", r="A", k="A")),
                 ("X" * 8193, "ERR LIMIT"), ("SNAP", state("SNAP", r="A", k="A"))])
    cases = [packaged("ingress", ["--downloads", "1"], rows)]
    rows = [("Z", state("Z")), ("D A 0", state("D")), ("D B 0", state("D")),
            ("ADD A 1", state("ADD", q="A", run="1,", trace="REMOVED:A:0:L0;DISPATCH:1:L0")),
            ("ADD A 1", state("ERR LIMIT", q="A", run="1,")),
            ("ADD B 1", state("ERR LIMIT", q="A", run="1,")),
            ("CACHE A", state("CACHE", q="A", r="A", k="A", run="1,")),
            ("REMOVED A 1", state("REMOVED", q="A", r="A", k="A", run="1,")),
            ("ADD A 0", state("ADD", run="1,", trace="REMOVED:A:1:L0")),
            ("STEP", state("STEP", trace="RUN:1;QSIZE:0:L0;LOG:Publish queue size: 0:L0")),
            ("STEP", state("EMPTY")),
            ("ADD B 1", state("ADD", q="B", run="2,", trace="REMOVED:B:0:L0;DISPATCH:2:L0")),
            ("STEP", state("STEP", w=1, call="1:B:0,", trace="RUN:2;QSIZE:0:L0;LOG:Publish queue size: 0:L0;WRITE:B:L0")),
            ("DONE 1 N", state("DONE", call="1:B:1,", trace="COMPLETE:1:1")),
            ("DONE 1 R", state("ERR CONSUMED", call="1:B:1,")),
            ("FIRE 1", state("ERR STALE", call="1:B:1,")),
            ("CLOSE", state("CLOSE")), ("ADD B 1", state("ERR CLOSED")), ("STEP", state("ERR CLOSED")),
            ("STOPFLAG 0", state("ERR CLOSED")), ("CLOSE", state("CLOSE")), ("Z", state("Z")),
            ("D A 0", state("D")), ("ADD A 1", state("ADD", q="A", run="1,", trace="REMOVED:A:0:L0;DISPATCH:1:L0"))]
    cases.append(packaged("capacity-cleanup", ["--downloads", "2", "--publications", "1", "--dispatches", "1",
                                                "--writes", "1", "--timers", "1"], rows))
    rows = [("Z", state("Z")), ("D A 0", state("D")), ("D B 0", state("D")), ("D C 0", state("D")),
            ("ADD A 1", state("ADD", q="A", run="1,", trace="REMOVED:A:0:L0;DISPATCH:1:L0")),
            ("ADD B 1", state("ADD", q="B,A", run="1,2,", trace="REMOVED:B:0:L0;DISPATCH:2:L0")),
            ("STEP", state("STEP", q="B,A", run="2,", trace="RUN:1;QSIZE:1:L0")),
            ("STEP", state("STEP", q="A", w=1, call="1:B:0,", trace="RUN:2;QSIZE:0:L0;LOG:Publish queue size: 1:L0;WRITE:B:L0")),
            ("ADD C 1", state("ADD", q="C,A", w=1, run="3,", call="1:B:0,", trace="REMOVED:C:0:L0;DISPATCH:3:L0")),
            ("ADD A 0", state("ERR LIMIT", q="C,A", w=1, run="3,", call="1:B:0,")),
            ("DONE 1 N", state("DONE", q="C,A", run="3,", tim="1:11000:0,", call="1:B:1,", clock=1,
                               trace="COMPLETE:1:1;CLOCK:0:L1;TIMER:Rating:write:delay:11000:L1")),
            ("FIRE 1", state("FIRE", q="C,A", run="3,2,", tim="1:11000:1,", call="1:B:1,", clock=1, trace="FIRE:1;DISPATCH:2:L0")),
            ("ADD B 1", state("ERR LIMIT", q="C,A", run="3,2,", tim="1:11000:1,", call="1:B:1,", clock=1)),
            ("STEP", state("STEP", q="C,A", run="2,", tim="1:11000:1,", call="1:B:1,", clock=1, trace="RUN:3;QSIZE:1:L0")),
            ("STEP", state("STEP", q="A", w=1, tim="1:11000:1,", call="1:B:1,2:C:0,", clock=1,
                           trace="RUN:2;QSIZE:0:L0;LOG:Publish queue size: 1:L0;WRITE:C:L0")),
            ("DONE 1 N", state("ERR CONSUMED", q="A", w=1, tim="1:11000:1,", call="1:B:1,2:C:0,", clock=1)),
            ("FIRE 1", state("ERR CONSUMED", q="A", w=1, tim="1:11000:1,", call="1:B:1,2:C:0,", clock=1)),
            ("DONE 2 N", state("ERR INPUT", q="A", w=1, tim="1:11000:1,", call="1:B:1,2:C:0,", clock=1)),
            ("TIME 2000", state("TIME", q="A", w=1, tim="1:11000:1,", call="1:B:1,2:C:0,")),
            ("DONE 2 N", state("DONE", q="A", tim="1:11000:1,2:12000:0,", call="1:B:1,2:C:1,", clock=1,
                               trace="COMPLETE:2:1;CLOCK:0:L1;TIMER:Rating:write:delay:12000:L1")),
            ("FIRE 2", state("FIRE", q="A", run="2,", tim="1:11000:1,2:12000:1,", call="1:B:1,2:C:1,", clock=1,
                            trace="FIRE:2;DISPATCH:2:L0")),
            ("STEP", state("STEP", w=1, tim="1:11000:1,2:12000:1,", call="2:C:1,3:A:0,", clock=1,
                           trace="RUN:2;QSIZE:0:L0;LOG:Publish queue size: 0:L0;WRITE:A:L0")),
            ("DONE 1 N", state("ERR STALE", w=1, tim="1:11000:1,2:12000:1,", call="2:C:1,3:A:0,", clock=1)),
            ("DONE 3 R", state("DONE", tim="1:11000:1,2:12000:1,", call="2:C:1,3:A:1,", clock=1, trace="COMPLETE:3:1"))]
    cases.append(packaged("completion-reservation", ["--downloads", "4", "--publications", "3", "--dispatches", "2",
                                                     "--writes", "2", "--timers", "2"], rows))
    rows = [("Z", state("Z")), ("D A 0", state("D")), ("D B 0", state("D")),
            ("ADD A 1", state("ADD", q="A", run="1,", trace="REMOVED:A:0:L0;DISPATCH:1:L0")),
            ("ADD B 1", state("ADD", q="B,A", run="1,2,", trace="REMOVED:B:0:L0;DISPATCH:2:L0")),
            ("RNG 1", state("RNG", q="B,A", run="1,2,")),
            ("ADD A 0", state("ERR INPUT", q="B,A", run="1,2,")),
            ("RNG 0", state("RNG", q="B,A", run="1,2,")),
            ("ADD A 0", state("ADD", q="A,B", run="1,2,3,", rng=1, trace="REMOVED:A:0:L0;RNG:1:L1;CHOICE:0;DISPATCH:3:L0")),
            ("ADD B 0", state("ERR INPUT", q="A,B", run="1,2,3,", rng=1)),
            ("MODE SYNC", state("MODE", q="A,B", run="1,2,3,", rng=1)),
            ("STEP", state("STEP", q="A,B", run="2,3,", rng=1, trace="RUN:1;QSIZE:2:L0")),
            ("STEP", state("STEP", q="A,B", run="3,", rng=1, trace="RUN:2;QSIZE:1:L0")),
            ("STEP", state("STEP", q="B", tim="1:11000:0,", call="1:A:1,", rng=1, clock=1,
                           trace="RUN:3;QSIZE:0:L0;LOG:Publish queue size: 1:L0;WRITE:A:L0;COMPLETE:1:1;CLOCK:0:L1;TIMER:Rating:write:delay:11000:L1")),
            ("DONE 1 N", state("ERR CONSUMED", q="B", tim="1:11000:0,", call="1:A:1,", rng=1, clock=1))]
    cases.append(packaged("scalar-and-inline", [], rows))
    invalid_arguments = [["--downloads", "0"], ["--downloads", "8193"], ["--publications", "0"],
                         ["--dispatches", "8193"], ["--writes", "0"], ["--timers", "8193"],
                         ["--unknown", "1"], ["--downloads"], ["--downloads", "-1"],
                         ["--downloads", "18446744073709551616"]]
    cases.extend(("arguments-" + str(i), args, b"", b"", 2) for i, args in enumerate(invalid_arguments))
    return cases


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("driver", nargs="?", type=pathlib.Path)
    parser.add_argument("--plan-only", action="store_true")
    args = parser.parse_args()
    cases = plan()
    commands = b"".join(item[2] for item in cases)
    expected = b"".join(item[3] for item in cases)
    count = sum(max(1, len(item[2].splitlines())) for item in cases)
    report = {"native_policy_observations": count, "source_equality_credit": 0,
              "cases": len(cases), "commands_sha256": hashlib.sha256(commands).hexdigest(),
              "expected_policy_sha256": hashlib.sha256(expected).hexdigest()}
    if args.plan_only:
        report["status"] = "planned; no native execution"
    else:
        assert args.driver is not None, "A native driver is required"
        result = run_cases(args.driver, cases, "biglybtc-rating-publication-cli-")
        report.update({"status": "passed", "driver_sha256": sha(args.driver),
                       "actual_sha256": result["actual_sha256"], "temporary_files_retained": False})
    print(json.dumps(report, sort_keys=True))


if __name__ == "__main__":
    main()
