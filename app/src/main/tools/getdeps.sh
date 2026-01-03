LOGF="getdeps.sh.log"
if [[ $# -ne 1 ]]; then
    echo "Usage: $0 DESTDIR" &>>$LOGF
    exit 1
fi
DESTDIR="$(printf '%s\n' "$1" | sed 's:/$::')"
FT_DEST="$DESTDIR/freetype"
HB_DEST="$DESTDIR/harfbuzz"
[[ ! -d "$DESTDIR" ]] && mkdir -p "$DESTDIR"
[[ -d "$FT_DEST" ]] && [[ -d "$HB_DEST" ]] && echo "$FT_DEST and $HB_DEST already exists" &>>$LOGF && exit 0

FT_V=2.14.1
FT_TAR="freetype-${FT_V}.tar.xz"
FT_DLURL="https://download.savannah.gnu.org/releases/freetype/${FT_TAR}"
HB_V=12.3.0
HB_TAR="harfbuzz-${HB_V}.tar.xz"
HB_DLURL="https://github.com/harfbuzz/harfbuzz/releases/download/${HB_V}/${HB_TAR}"

if ! command -v curl &>>$LOGF; then
    echo "curl not installed" &>>$LOGF
    exit 1
fi
if ! command -v tar &>>$LOGF; then
    echo "tar not installed" &>>$LOGF
    exit 1
fi

ensure_dep() {
    local dest="$1"
    local tar="$2"
    local url="$3"
    local topents=()
    if [[ ! -f "$tar" ]]; then
        curl -L -o "$tar" "$url" &>>$LOGF || return 2
    fi
    mapfile -t topents < <(
        tar -tf "$tar" \
        | sed 's:/*$::' \
        | cut -d/ -f1 \
        | sort -u
    )
    printf '"%s" topents:%d:\n%s\n' "$tar" ${#topents[@]} "${topents[@]}" &>>$LOGF
    if [[ ${#topents[@]} -ne 1 ]]; then
        echo "Unexpected amount of topentries(${#topents[@]}) in $tar" &>>$LOGF
        return 3
    fi
    tar -xf "$tar" -C "$(dirname "$dest")" &>>$LOGF || return 4
    ln -s "${topents[0]}" "$dest" || return 5
    rm -f "$tar" && return 0
}

if [[ ! -e "$FT_DEST" ]]; then
    ensure_dep "$FT_DEST" "$DESTDIR/$FT_TAR" "$FT_DLURL"
    rv=$?; [[ $ec -ne 0 ]] && exit $rv
else
    echo "$FT_DEST already exists" &>>$LOGF
fi
if [[ ! -e "$HB_DEST" ]]; then
    ensure_dep "$HB_DEST" "$DESTDIR/$HB_TAR" "$HB_DLURL"
    rv=$?; [[ $ec -ne 0 ]] && exit $rv
else
    echo "$HB_DEST already exists" &>>$LOGF
fi
exit 0