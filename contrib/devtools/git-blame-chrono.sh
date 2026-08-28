#!/usr/bin/env bash
# Usage: git-blame-chrono <file>

if [ -z "$1" ]; then
  echo "Usage: $0 <file>"
  exit 1
fi

git blame --line-porcelain --date=iso-strict "$1" | awk '
  /^author-time / { ts = $2 }
  /^author /      { author = substr($0, 8) }
  /^\t/ {
    # Portable way to convert Unix timestamp
    if (system("date -r " ts " >/dev/null 2>&1") == 0) {
      # macOS / BSD
      cmd = "date -r " ts " \"+%Y-%m-%d %H:%M\""
    } else {
      # GNU / Linux
      cmd = "date -d @" ts " \"+%Y-%m-%d %H:%M\""
    }
    cmd | getline date
    close(cmd)
    printf "%s  %-22s  %s\n", date, author, substr($0, 2)
  }
' | sort | less +G
