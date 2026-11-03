#!/usr/bin/env bash
set -euo pipefail

stage_dir="${1:-stage}"
package_dir="${2:-package}"
archive_path="${3:-}"
ref="${GITHUB_REF_NAME:-manual}"
ref="${ref//\//-}"
if [[ -z "$archive_path" ]]; then
    archive_path="ArenaMP-Linux-x86_64-SteamDeck-${ref}.tar.gz"
fi

rm -rf "$package_dir"
mkdir -p "$package_dir" "$package_dir/lib" "$package_dir/userdata"

copy_if_exists() {
    local src="$1"
    local dst="$2"
    if [[ -d "$src" ]]; then
        mkdir -p "$dst"
        cp -a "$src/." "$dst/"
    elif [[ -e "$src" ]]; then
        mkdir -p "$(dirname "$dst")"
        cp -a "$src" "$dst"
    fi
}

copy_first_existing() {
    local dst="$1"
    shift
    local src
    for src in "$@"; do
        if [[ -e "$src" ]]; then
            mkdir -p "$(dirname "$dst")"
            cp -a "$src" "$dst"
            return 0
        fi
    done
    return 1
}

# CMake installs Linux as a normal /usr-style tree. The release archive must be
# portable, so flatten binaries/config/resources back to a single runnable root.
if [[ -d "$stage_dir/bin" ]]; then
    cp -a "$stage_dir/bin/." "$package_dir/"
fi
find "$stage_dir" -maxdepth 1 -type f -perm -111 -exec cp -a {} "$package_dir/" \; 2>/dev/null || true

copy_first_existing "$package_dir/openmw.cfg" \
    "$stage_dir/etc/openmw/openmw.cfg" \
    "$stage_dir/openmw.cfg" \
    "$stage_dir/openmw.cfg.install" || true
copy_first_existing "$package_dir/defaults.bin" \
    "$stage_dir/etc/openmw/defaults.bin" \
    "$stage_dir/defaults.bin" || true
copy_first_existing "$package_dir/gamecontrollerdb.txt" \
    "$stage_dir/etc/openmw/gamecontrollerdb.txt" \
    "$stage_dir/gamecontrollerdb.txt" || true
copy_first_existing "$package_dir/tes3mp-client-default.cfg" \
    "$stage_dir/etc/openmw/tes3mp-client-default.cfg" \
    "$stage_dir/tes3mp-client-default.cfg" || true
copy_first_existing "$package_dir/tes3mp-server-default.cfg" \
    "$stage_dir/etc/openmw/tes3mp-server-default.cfg" \
    "$stage_dir/tes3mp-server-default.cfg" || true
copy_if_exists "$stage_dir/share/games/openmw/resources" "$package_dir/resources"
copy_if_exists "$stage_dir/resources" "$package_dir/resources"
copy_if_exists "$stage_dir/share/games/openmw/data" "$package_dir/data"
copy_if_exists "$stage_dir/data" "$package_dir/data"
copy_if_exists "$stage_dir/share/doc" "$package_dir/doc"
copy_if_exists "$stage_dir/share/licenses" "$package_dir/licenses"
copy_if_exists "$stage_dir/lib" "$package_dir/lib"

# Use the ArenaMP server core bundled with this exact source revision. Prefer
# the CMake install tree and fall back to the checked-out source tree; never
# download a different CoreScripts revision while creating a release artifact.
copy_if_exists "$stage_dir/share/games/openmw/server" "$package_dir/server"
if [[ ! -d "$package_dir/server" && -d "server" ]]; then
    cp -a server "$package_dir/server"
fi
for required_server_file in \
    scripts/serverCore.lua \
    scripts/config.lua \
    data/banlist.json \
    data/requiredDataFiles.json \
    ARENAMP_CORE_VERSION.txt; do
    if [[ ! -f "$package_dir/server/$required_server_file" ]]; then
        echo "ERROR: bundled ArenaMP server core file is missing from the Linux package: $required_server_file" >&2
        exit 1
    fi
done
for required_data_dir in cell custom map player recordstore world; do
    [[ -d "$package_dir/server/data/$required_data_dir" ]] || {
        echo "ERROR: bundled ArenaMP server data directory is missing from the Linux package: $required_data_dir" >&2
        exit 1
    }
done
# Windows-only Lua modules cannot be loaded by the Linux server and are omitted.
find "$package_dir/server" -type f -iname '*.dll' -delete
# Repository-only marker files keep empty runtime directories in Git but must
# not be exposed as part of the shipped server data.
find "$package_dir/server" -type f \
    \( -name '.gitignore' -o -name '.gitkeep' \) -delete

# Copy runtime libraries like the official GNU/Linux tarball. Ubuntu runners
# link against system MyGUI/OSG/Bullet/Boost/etc.; without bundling them the
# archive works on the runner but misses lib/ on SteamOS/other distros.
search_dirs=(
    /lib
    /usr/lib
    /usr/local/lib
    /usr/local/lib64
    /lib/x86_64-linux-gnu
    /usr/lib/x86_64-linux-gnu
    /usr/local/lib/x86_64-linux-gnu
)

copy_library_pattern() {
    local pattern="$1"
    local dir
    for dir in "${search_dirs[@]}"; do
        [[ -d "$dir" ]] || continue
        find "$dir" -maxdepth 2 -name "$pattern" -exec cp -a --preserve=links {} "$package_dir/lib/" \; 2>/dev/null || true
    done
}

runtime_patterns=(
    'libboost_thread.so*'
    'libboost_system.so*'
    'libboost_filesystem.so*'
    'libboost_program_options.so*'
    'libboost_iostreams.so*'
    'libBulletCollision.so*'
    'libLinearMath.so*'
    'libBulletCollision-float64.so*'
    'libLinearMath-float64.so*'
    'libMyGUIEngine.so*'
    'libOpenThreads.so*'
    'libosg.so*'
    'libosgAnimation.so*'
    'libosgDB.so*'
    'libosgFX.so*'
    'libosgGA.so*'
    'libosgParticle.so*'
    'libosgShadow.so*'
    'libosgText.so*'
    'libosgUtil.so*'
    'libosgViewer.so*'
    'libosgWidget.so*'
    'libSDL2*.so*'
    'libopenal.so*'
    'libavcodec.so*'
    'libavformat.so*'
    'libavutil.so*'
    'libswresample.so*'
    'libswscale.so*'
    'libpng16.so*'
    'libbz2.so*'
    'libunshield.so*'
    'libuuid.so*'
    'libtinfo.so*'
    'libtinyxml.so*'
    'liblua5.1.so*'
    'libluajit-5.1.so*'
    'libsndio.so*'
    'libssh*.so*'
    'libvpx.so*'
    'libwebp.so*'
    'libx264.so*'
    'libx265.so*'
    'libaom.so*'
    'libcodec2.so*'
    'libshine.so*'
    'libcrystalhd.so*'
)
for pattern in "${runtime_patterns[@]}"; do
    copy_library_pattern "$pattern"
done

# Copy additional direct ELF dependencies discovered by ldd, but do not bundle
# glibc/loader pieces that should come from the target system.
copy_ldd_dependency() {
    local lib="$1"
    local base
    [[ -f "$lib" ]] || return 0
    base="$(basename "$lib")"
    case "$base" in
        libc.so.*|libm.so.*|libpthread.so.*|librt.so.*|libdl.so.*|libresolv.so.*|ld-linux*.so*|libgcc_s.so.*)
            return 0
            ;;
    esac
    cp -aL "$lib" "$package_dir/lib/" 2>/dev/null || true
}

while IFS= read -r -d '' exe; do
    if file "$exe" | grep -q 'ELF .* executable'; then
        ldd "$exe" 2>/dev/null | awk '/=> \/.*\.so/ {print $3} /^\/.*\.so/ {print $1}' | while read -r lib; do
            copy_ldd_dependency "$lib"
        done
    fi
done < <(find "$package_dir" -maxdepth 1 -type f -perm -111 -print0)

# OSG loads format plugins dynamically at runtime; they usually do not show up
# in ldd output for tes3mp/openmw-launcher, so copy the plugin directory too.
for dir in /usr/lib/x86_64-linux-gnu/osgPlugins-* /usr/local/lib/osgPlugins-* /usr/lib/osgPlugins-*; do
    [[ -d "$dir" ]] || continue
    cp -a "$dir" "$package_dir/lib/"
done


# Qt launcher/wizard may need platform plugins on systems without matching Qt.
for qtbase in /usr/lib/x86_64-linux-gnu/qt5/plugins /usr/lib/qt5/plugins /usr/local/lib/qt5/plugins; do
    [[ -d "$qtbase" ]] || continue
    for subdir in platforms imageformats iconengines platformthemes xcbglintegrations styles; do
        [[ -d "$qtbase/$subdir" ]] || continue
        mkdir -p "$package_dir/lib/qt5/plugins/$subdir"
        cp -a "$qtbase/$subdir/." "$package_dir/lib/qt5/plugins/$subdir/"
    done
done

cat > "$package_dir/tes3mp-prelaunch" <<'PRELAUNCH'
#!/usr/bin/env bash
set -euo pipefail

wrapper="${1:-}"
gamedir="$(cd "$(dirname "$0")" && pwd -P)"
userdata="$gamedir/userdata"
mkdir -p "$userdata"

# Prefer portable userdata for cfg files only. The server core always runs
# directly from the bundled server directory next to the binaries.
case "$wrapper" in
    tes3mp-server)
        if [[ ! -f "$userdata/tes3mp-server.cfg" && -f "$gamedir/tes3mp-server-default.cfg" ]]; then
            cp -f "$gamedir/tes3mp-server-default.cfg" "$userdata/tes3mp-server.cfg"
        fi
        ;;
    tes3mp|tes3mp-browser|openmw-launcher|openmw-wizard|*)
        if [[ ! -f "$userdata/tes3mp-client.cfg" && -f "$gamedir/tes3mp-client-default.cfg" ]]; then
            cp -f "$gamedir/tes3mp-client-default.cfg" "$userdata/tes3mp-client.cfg"
        fi
        ;;
esac
PRELAUNCH
chmod +x "$package_dir/tes3mp-prelaunch"

wrap_binary() {
    local bin="$1"
    local path="$package_dir/$bin"
    [[ -f "$path" && -x "$path" ]] || return 0
    file "$path" | grep -q 'ELF .* executable' || return 0
    mv "$path" "$path.x86_64"
    cat > "$path" <<'WRAPPER'
#!/usr/bin/env bash
set -euo pipefail
wrapper="$(basename "$0")"
gamedir="$(cd "$(dirname "$0")" && pwd -P)"
cd "$gamedir"
if [[ -f ./tes3mp-prelaunch ]]; then
    bash ./tes3mp-prelaunch "$wrapper"
fi
export LD_LIBRARY_PATH="$gamedir/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$gamedir/lib/qt5/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"
exec "$gamedir/$wrapper.x86_64" "$@"
WRAPPER
    chmod +x "$path"
}

for bin in tes3mp tes3mp-browser tes3mp-server openmw-launcher openmw-wizard openmw-iniimporter openmw-essimporter bsatool esmtool; do
    wrap_binary "$bin"
done

cat > "$package_dir/STEAM_DECK_README.txt" <<'README'
ArenaMP Linux x86_64 portable build for Steam Deck Desktop Mode / SteamOS.
Run ./openmw-launcher from this folder, or add it as a Non-Steam Game.
Runtime libraries are bundled in ./lib and loaded through the wrapper scripts.
Morrowind data files are not included.
README

# Preserve stable permissions inside the tarball.
find "$package_dir" -type d -exec chmod 755 {} \;
find "$package_dir" -type f -name '*.sh' -exec chmod 755 {} \; 2>/dev/null || true

rm -f "$archive_path"
tar --numeric-owner -C "$package_dir" -czf "$archive_path" .
printf 'Created %s\n' "$archive_path"
