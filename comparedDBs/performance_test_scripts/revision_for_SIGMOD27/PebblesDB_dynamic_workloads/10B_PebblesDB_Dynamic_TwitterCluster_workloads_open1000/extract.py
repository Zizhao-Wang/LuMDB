import re
from pathlib import Path

try:
    current_dir = Path(__file__).resolve().parent
except NameError:
    current_dir = Path.cwd()

print(f"Scanning directory: {current_dir}")

log_files = sorted(current_dir.glob("*.log"))

if not log_files:
    raise FileNotFoundError("No .log files found in the current directory.")

print("Found log files:")
for f in log_files:
    print(f"  - {f.name}")

# =========================================================
# Match cumulative ops/time for each sample point, then compute
# interval throughput from the delta to the previous sample.
#
# Example:
# thread 0: (10000000,140000000) ops and (18034.7,59516.5)
# ops/second in (554.485871,2352.290088) seconds
#
# Interval throughput =
#   (140000000 - 130000000) / (2352.290088 - 1797.804217)
# =========================================================
number_pattern = r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?"

sample_pattern = re.compile(
    rf"thread\s+0:\s*"
    rf"\(\s*(?P<interval_ops>{number_pattern})\s*,\s*(?P<total_ops>{number_pattern})\s*\)\s*ops\s+and\s*"
    rf"\(\s*{number_pattern}\s*,\s*{number_pattern}\s*\)\s*ops/second\s+in\s*"
    rf"\(\s*(?P<interval_seconds>{number_pattern})\s*,\s*(?P<total_seconds>{number_pattern})\s*\)\s*seconds",
    re.IGNORECASE
)

def sanitize_var_name(filename: str) -> str:
    name = Path(filename).stem
    name = re.sub(r"[^0-9a-zA-Z_]+", "_", name)
    if re.match(r"^\d", name):
        name = "_" + name
    return name


def format_groups(values, group_size=10):
    groups = [
        values[i:i + group_size]
        for i in range(0, len(values), group_size)
    ]

    lines = []
    lines.append("np.array([")

    for group in groups:
        values_str = ", ".join(f"{v:.2f}" for v in group)

        if len(group) == group_size:
            lines.append(f"    ({values_str}),")
        else:
            lines.append(f"    ({values_str}),  # incomplete group: {len(group)} values")

    lines.append("])")
    return "\n".join(lines)


points_per_group = 10
all_results = {}

print("\n" + "=" * 80)
print("Per-file throughput extraction")
print("=" * 80)

for log_file in log_files:
    with open(log_file, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()

    values = []
    prev_total_ops = None
    prev_total_seconds = None

    for match in sample_pattern.finditer(content):
        interval_ops = float(match.group("interval_ops"))
        total_ops = float(match.group("total_ops"))
        interval_seconds = float(match.group("interval_seconds"))
        total_seconds = float(match.group("total_seconds"))

        if (
            prev_total_ops is not None
            and prev_total_seconds is not None
            and total_ops > prev_total_ops
            and total_seconds > prev_total_seconds
        ):
            ops_delta = total_ops - prev_total_ops
            seconds_delta = total_seconds - prev_total_seconds
            throughput = ops_delta / seconds_delta
        else:
            # First sample or counter reset: fall back to this sample's interval stats.
            throughput = interval_ops / interval_seconds

        values.append(throughput)
        prev_total_ops = total_ops
        prev_total_seconds = total_seconds

    all_results[log_file.name] = values
    var_name = sanitize_var_name(log_file.name)

    print(f"\n# ------------------------------------------------------------")
    print(f"# File: {log_file.name}")
    print(f"# Matched throughput values: {len(values)}")
    print(f"# ------------------------------------------------------------")

    if len(values) == 0:
        print("# No throughput values matched in this file.")
        continue

    if len(values) % points_per_group != 0:
        print(
            f"# Warning: {len(values)} values is not divisible by {points_per_group}. "
            f"The last group will be incomplete."
        )

    print(f"{var_name} = {format_groups(values, group_size=points_per_group)}")


print("\n" + "=" * 80)
print("Summary")
print("=" * 80)

total = 0
for fname, values in all_results.items():
    total += len(values)
    print(f"{fname}: {len(values)} throughput values")

print(f"\nTotal throughput values across all files: {total}")
