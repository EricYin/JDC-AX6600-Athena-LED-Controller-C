#!/bin/bash
set -e

if [ -z "$1" ]; then
    echo "Usage: $0 <version> (e.g. 0.4.1)"
    exit 1
fi

NEW_VER="$1"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"

echo "Bumping version to ${NEW_VER} across project..."

# 1. VERSION file
echo "${NEW_VER}" > "${REPO_DIR}/VERSION"

# 2. include/app.h
sed -i "s/#define ATHENA_LED_VERSION \".*\"/#define ATHENA_LED_VERSION \"${NEW_VER}\"/" "${REPO_DIR}/include/app.h"

# 3. athena-led-c/Makefile
sed -i "s/PKG_VERSION:=.*/PKG_VERSION:=${NEW_VER}/" "${REPO_DIR}/athena-led-c/Makefile"

# 4. luci-app-athena-led-c/Makefile
sed -i "s/PKG_VERSION:=.*/PKG_VERSION:=${NEW_VER}/" "${REPO_DIR}/luci-app-athena-led-c/Makefile"

# 5. luci-app-athena-led-c settings.js
sed -i "s/var LUCI_APP_VERSION = '.*';/var LUCI_APP_VERSION = '${NEW_VER}';/" "${REPO_DIR}/luci-app-athena-led-c/htdocs/luci-static/resources/view/athena_led_c/settings.js"
sed -i "s/var CORE_VERSION = '.*';/var CORE_VERSION = '${NEW_VER}';/" "${REPO_DIR}/luci-app-athena-led-c/htdocs/luci-static/resources/view/athena_led_c/settings.js"

echo "Version successfully bumped to ${NEW_VER}!"
