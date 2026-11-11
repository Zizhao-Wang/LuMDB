import re
from pathlib import Path


# =========================================================
# 1. Basic configuration
# =========================================================

# 每 10 个点输出一行
POINTS_PER_ROW = 10

# 输出小数位数
ROUND_DIGITS = 3

# True  = 搜索当前目录 + 所有子目录
# False = 只搜索当前目录
SEARCH_RECURSIVELY = True

# 是否打印被跳过的 log 文件
# 如果 log 文件很多，建议 False，终端会干净很多
PRINT_SKIPPED_FILES = False


# =========================================================
# 2. Automatically detect script directory
# =========================================================
try:
    SCRIPT_DIR = Path(__file__).resolve().parent
except NameError:
    SCRIPT_DIR = Path.cwd()


# =========================================================
# 3. Regex patterns
# =========================================================
waf_pattern = re.compile(
    r"WriteAmplification:\s*([\d.]+)",
    re.IGNORECASE,
)


# =========================================================
# 4. Find log files
# =========================================================
def find_log_files(root_dir):
    if SEARCH_RECURSIVELY:
        log_files = list(root_dir.rglob("*.log"))
    else:
        log_files = list(root_dir.glob("*.log"))

    return sorted(log_files, key=lambda p: str(p).lower())


# =========================================================
# 5. Extract WAF from one log file
# =========================================================
def extract_waf_from_one_log(log_path):
    waf_values = []

    with open(log_path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            waf_match = waf_pattern.search(line)
            if waf_match:
                waf_values.append(float(waf_match.group(1)))

    return waf_values


# =========================================================
# 6. Formatting helpers
# =========================================================
def make_safe_var_name(path):
    """
    Convert log filename to a safe Python variable name.

    Example:
        leveldb-zipf.log -> leveldb_zipf
    """
    name = path.stem
    name = re.sub(r"[^a-zA-Z0-9_]+", "_", name)
    name = re.sub(r"_+", "_", name)
    name = name.strip("_")

    if not name:
        name = "log_file"

    if name[0].isdigit():
        name = "log_" + name

    return name


def print_values_as_np_array(var_name, values):
    print(f"{var_name} = np.array([")

    for i in range(0, len(values), POINTS_PER_ROW):
        row = values[i:i + POINTS_PER_ROW]
        row_str = ", ".join(f"{v:.{ROUND_DIGITS}f}" for v in row)

        if len(row) < POINTS_PER_ROW:
            print(f"    [{row_str}],  # incomplete row: {len(row)} points")
        else:
            print(f"    [{row_str}],")

    print("])")
    print()


# =========================================================
# 7. Main
# =========================================================
def main():
    log_files = find_log_files(SCRIPT_DIR)

    print("=" * 80)
    print("Searching .log files under:")
    print(f"  {SCRIPT_DIR}")
    print(f"Found {len(log_files)} .log files")
    print("=" * 80)
    print()

    print("import numpy as np")
    print()

    valid_count = 0

    for log_file in log_files:
        values = extract_waf_from_one_log(log_file)

        # 没有提取到 WAF 的 log 文件直接跳过
        if len(values) == 0:
            if PRINT_SKIPPED_FILES:
                print(f"# [SKIP] {log_file.relative_to(SCRIPT_DIR)}")
                print()
            continue

        valid_count += 1

        relative_name = str(log_file.relative_to(SCRIPT_DIR)).replace("\\", "/")
        var_name = make_safe_var_name(log_file)

        print(f"# {relative_name}")
        print(f"# extracted {len(values)} WAF points")

        if len(values) % POINTS_PER_ROW != 0:
            print(
                f"# WARNING: {len(values)} points is not divisible by "
                f"{POINTS_PER_ROW}; last row is incomplete"
            )

        print_values_as_np_array(var_name, values)

    print("=" * 80)
    print(f"Valid log files with WAF data: {valid_count}")
    print("=" * 80)


if __name__ == "__main__":
    main()
