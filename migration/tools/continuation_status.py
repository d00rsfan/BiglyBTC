#!/usr/bin/env python3
"""Print compact saved continuation state without reading build logs or running subjects."""

import hashlib
import json
import pathlib
import subprocess


def main():
    root = pathlib.Path(__file__).resolve().parents[2]
    state = json.loads((root / "migration/continuation.json").read_text())
    print("Saved state:", state["updated_utc"])
    print("Task:", state["objective"])
    print("Native expected HEAD:", state["native_head"])
    actual = subprocess.check_output(
        ["git", "-C", str(root.parent / "BiglyBTC"), "rev-parse", "HEAD"],
        text=True,
    ).strip()
    qualifier = state.get("qualified_native_source", state["native_head"])
    repo = root.parent / "BiglyBTC"
    trees = [subprocess.check_output(
        ["git", "-C", str(repo), "rev-parse", commit + ":preview/native"],
        text=True,
    ).strip() for commit in (actual, qualifier)]
    label = "MATCH" if actual == state["native_head"] else (
        "METADATA ONLY" if trees[0] == trees[1] else "CHANGED")
    print("Native actual HEAD:", actual, label)
    print("Qualification:", state["current_source_qualification"])
    for item in state["receipts"]:
        path = root / item["path"]
        if not path.exists():
            print(item["name"] + ": PENDING", item["path"])
            continue
        data = path.read_bytes()
        if not data:
            print(item["name"] + ": EMPTY", item["path"])
            continue
        digest = hashlib.sha256(data).hexdigest()
        if item.get("sha256") and digest != item["sha256"]:
            print(item["name"] + ": PIN CHANGED", item["path"])
            continue
        try:
            value = json.loads(data)
        except json.JSONDecodeError:
            print(item["name"] + ": INCOMPLETE OR INVALID JSON", item["path"])
            continue
        if isinstance(value, dict):
            status = value.get("status", "NO STATUS")
        elif isinstance(value, list):
            status = "; ".join(
                str(row.get("workflowName", "workflow")) + ": "
                + str(row.get("conclusion") or row.get("status", "NO STATUS"))
                for row in value if isinstance(row, dict)
            ) or "EMPTY LIST"
        else:
            status = "JSON RECEIPT"
        print(item["name"] + ":", status)
    print("Active local sessions:", state["active_local_sessions"] or "none recorded")
    print("Remote CI (last observed):", state["remote_ci"])
    print("Next original checkpoint parent:", state["original_checkpoint_expected_parent"])
    for name, task in state["parallel_source"].items():
        print(name + ":", task)
    print("Next:", state["next_action"])
    print("Question:", state["pending_question"])
    print("Details:", state["details"])


if __name__ == "__main__":
    main()
