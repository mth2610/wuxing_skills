#!/usr/bin/env bash
# Gate 4 helper: flip the hue-preserving tone-map candidate WHILE THE GAME RUNS.
#   third_party/vulkan/docs/BRIGHT_BACKGROUND_VFX_SPEC.md §12.1
#
# core/tuning.c watches tuning.cfg's mtime and main.c calls Tuning_Update() every frame,
# so writing the key takes effect on the next frame — no restart, no rebuild. That is the
# whole reason gate 4 is judged live and in motion instead of from static screenshots: a
# still frame of a VFX is not what anyone plays.
#
#   scripts/set_tonemap.sh off        remove overrides (back to the SHIPPING 0.6, clean cfg)
#   scripts/set_tonemap.sh 0.6        set a specific strength
#   scripts/set_tonemap.sh shoulder   the gate-3 diagnostic view (magenta = active band)
#   scripts/set_tonemap.sh blind      pick one of 0 / 0.35 / 0.6 / 1.0 at random and HIDE it
#   scripts/set_tonemap.sh reveal     print what blind chose, then clear it
#
# `blind` exists because you cannot un-know which image is the new one. Judging an A/B you
# can identify is how every "the new one looks better" result gets produced. Flip blind a
# few times, write down what you think each looked like, then reveal.
set -euo pipefail
cd "$(dirname "$0")/.."

CFG=tuning.cfg
KEY=postfx_hue_restore
ANSWER=.tonemap_blind_answer

write_key() {   # write_key <value|"">
  local tmp; tmp=$(mktemp)
  [ -e "$CFG" ] && grep -v "^[[:space:]]*$KEY[[:space:]]*=" "$CFG" > "$tmp" || : > "$tmp"
  [ -n "$1" ] && echo "$KEY = $1" >> "$tmp"
  mv "$tmp" "$CFG"
}

case "${1:-}" in
  off)
      write_key ""
      tmp=$(mktemp); grep -v "postfx_shoulder_view" "$CFG" > "$tmp" 2>/dev/null || : > "$tmp"
      mv "$tmp" "$CFG"
      echo "overrides removed — back to the shipping default ($KEY = 0.6), tuning.cfg clean" ;;
  shoulder)
      # Its own knob since §12.1 shipped: hue restore now defaults to 0.6, so a negative
      # sentinel on that knob would be both fragile and confusing.
      tmp=$(mktemp); grep -v "postfx_shoulder_view" "$CFG" > "$tmp" 2>/dev/null || : > "$tmp"
      mv "$tmp" "$CFG"; echo "postfx_shoulder_view = 1" >> "$CFG"
      echo "postfx_shoulder_view = 1 — magenta = the tone map's active band" ;;
  blind)
      OPTS=(0 0.35 0.6 1.0)   # 0.6 is what shipped; the rest remain for re-testing
      PICK=${OPTS[$((RANDOM % ${#OPTS[@]}))]}
      write_key "$PICK"
      echo "$PICK" > "$ANSWER"
      echo "blind value applied — judge it, then: scripts/set_tonemap.sh reveal" ;;
  reveal)
      [ -f "$ANSWER" ] || { echo "no blind value pending"; exit 1; }
      echo "blind value was: $(cat "$ANSWER")"; rm -f "$ANSWER" ;;
  ''|-h|--help) sed -n '2,20p' "$0"; exit 2 ;;
  *)        write_key "$1";    echo "$KEY = $1" ;;
esac

# The persisted-override landmine: tuning.cfg survives sessions, so a value left here
# silently changes what every later visual judgement is made against.
grep -q "^[[:space:]]*$KEY" "$CFG" 2>/dev/null &&
  echo "NOTE: tuning.cfg now carries $KEY — run 'scripts/set_tonemap.sh off' when you are done." || true
