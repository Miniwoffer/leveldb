#!/usr/bin/env bash
set -o pipefail
 find . -regextype egrep -type f -regex '[.]/(db|include|issues|port|benchmarks|helpers|util|table)/.+[.](cc|cpp|hpp|h)' | xargs clang-format --dry-run -i --style=file -Werror

