#!/bin/sh
# Add canonical aliases; never remove/overwrite existing programs. Keep VS variants separate.
# Usage: tools/psdefs_rename_gxp.sh DIRECTORY [RENAME_LIST]
set -eu
[ "$#" -ge 1 ] && [ "$#" -le 2 ] || { echo "usage: $0 DIRECTORY [RENAME_LIST]" >&2; exit 2; }
dir=$(cd "$1" && pwd)
list=${2:-"$(dirname "$0")/psdef_rename.txt"}
count=0
while read -r old new rest; do
    case "$old" in ''|'#'*) continue ;; esac
    case "$old$new" in *[!0-9A-Fa-f]*) echo "invalid hash in $list" >&2; exit 1 ;; esac
    [ "${#old}" -eq 8 ] && [ "${#new}" -eq 8 ] || exit 1
    [ "$old" != "$new" ] || continue
    for src in "$dir/ps_$old.gxp" "$dir/ps_$old.frag.gxp" "$dir/ps_${old}_"*.gxp; do
        [ -f "$src" ] || continue
        suffix=${src#"$dir/ps_$old"}
        dst="$dir/ps_$new$suffix"
        [ ! -e "$dst" ] || continue
        ln "$src" "$dst" 2>/dev/null || cp -p "$src" "$dst"
        count=$((count + 1))
    done
done < "$list"
echo "Created $count canonical GXP aliases (existing files retained)."
