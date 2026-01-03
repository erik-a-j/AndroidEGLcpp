
if [[ $# -ne 1 ]]; then
    echo "Usage: $0 DESTDIR" >&2
    exit 1
fi
FONTSDIR="/usr/share/fonts"
FONT="Roboto-Regular.ttf"
DESTDIR="$(printf '%s\n' "$1" | sed 's:/$::')"
DEST="$DESTDIR/$FONT"
[[ -f "$DEST" ]] && exit 0

if [[ -n "$TERMUX_VERSION" ]]; then
    FONTSDIR="/system/fonts"
fi

FONT_PATH="$(find "$FONTSDIR" -type f -name "$FONT" -print -quit 2>/dev/null)"
if [[ -n "$FONT_PATH" ]]; then
    echo "Font found: $FONT_PATH" >&2
    echo "Copying $FONT_PATH" >&2
    echo "        -> $DEST" >&2
    cp -a "$FONT_PATH" "$DEST"
else
    echo "Font not found: $FONT" >&2
    exit 1
fi