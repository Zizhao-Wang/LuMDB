#!/usr/bin/env bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
YCSB_WORKLOAD=E "$SCRIPT_DIR/zipf_workload_YCSB_common.sh"
