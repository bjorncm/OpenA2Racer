#!/bin/bash
# Copies the libraries an app bundle's program needs from outside macOS
# (Homebrew's SDL3, FreeType and theirs) into Contents/Frameworks, points
# every link at those copies and signs the bundle ad hoc.
# Usage: bundle-mac-libraries.sh <App.app>
set -euo pipefail

app="$1"
program="$app/Contents/MacOS/$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$app/Contents/Info.plist")"
frameworks="$app/Contents/Frameworks"
mkdir -p "$frameworks"

# The libraries a Mach-O file links to, one per line.
links() {
    otool -L "$1" | tail -n +2 | awk '{print $1}'
}

# Resolves @rpath, @loader_path and @executable_path for the link $2 of the
# file whose original (before copying) is $1.
resolve() {
    local file="$1" link="$2" dir
    dir="$(dirname "$file")"
    case "$link" in
    @loader_path/*) echo "$dir/${link#@loader_path/}" ;;
    @executable_path/*) echo "$(dirname "$program")/${link#@executable_path/}" ;;
    @rpath/*)
        local name="${link#@rpath/}" rpath
        while read -r rpath; do
            rpath="${rpath/@loader_path/$dir}"
            if [ -f "$rpath/$name" ]; then
                echo "$rpath/$name"
                return
            fi
        done < <(otool -l "$file" | awk '/LC_RPATH/ {getline; getline; print $2}')
        # Homebrew libraries find each other in the same folder.
        [ -f "$dir/$name" ] && echo "$dir/$name"
        ;;
    *) echo "$link" ;;
    esac
}

# Not from macOS itself: to be copied.
foreign() {
    case "$1" in
    /usr/lib/* | /System/*) return 1 ;;
    @executable_path/../Frameworks/*) return 1 ;;
    *) return 0 ;;
    esac
}

# Pairs of (file in the bundle, its original), to visit.
queue=("$program" "$program")
done_files=" "
while [ ${#queue[@]} -gt 0 ]; do
    file="${queue[0]}"
    original="${queue[1]}"
    queue=("${queue[@]:2}")
    case "$done_files" in *" $file "*) continue ;; esac
    done_files="$done_files$file "
    while read -r link; do
        foreign "$link" || continue
        source="$(resolve "$original" "$link" || true)"
        if [ -z "$source" ] || [ ! -f "$source" ]; then
            echo "Cannot find $link (needed by $file)" >&2
            exit 1
        fi
        name="$(basename "$link")"
        target="$frameworks/$name"
        if [ ! -f "$target" ]; then
            cp -L "$source" "$target"
            chmod u+w "$target"
            install_name_tool -id "@executable_path/../Frameworks/$name" "$target" 2>/dev/null
            queue+=("$target" "$source")
        fi
        install_name_tool -change "$link" "@executable_path/../Frameworks/$name" "$file" 2>/dev/null
    done < <(links "$file" | grep -v "^$(otool -D "$file" | tail -n +2)$" || true)
done

# codesign refuses files with Finder information or similar attributes.
# iCloud Drive (a synced Desktop) can add them back at any moment, so strip
# and sign again a few times.
for attempt in 1 2 3; do
    xattr -cr "$app"
    if codesign --force --sign - "$frameworks"/*.dylib && codesign --force --sign - "$app"; then
        exit 0
    fi
    sleep 1
done
exit 1
