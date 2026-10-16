#!/bin/bash
set -e

REPO_DIR="/data00/repos/JDC-AX6600-Athena-LED-Controller-C"
IM_DIR="${IM_DIR:-/data00/repos/immortalwrt}"
VERSION="${1:-$(cat "${REPO_DIR}/VERSION" 2>/dev/null || grep -oE '[0-9]+\.[0-9]+\.[0-9]+' "${REPO_DIR}/include/app.h" | head -n1)}"

echo "Building distribution for athena-led-c v${VERSION}..."
DIST_DIR="${REPO_DIR}/dist"
STAGING_HOST="${IM_DIR}/staging_dir/hostpkg/bin"
TOOLCHAIN_BIN="${IM_DIR}/staging_dir/toolchain-aarch64_cortex-a53_gcc-14.3.0_musl/bin"
TARGET_CC="${TOOLCHAIN_BIN}/aarch64-openwrt-linux-musl-gcc"
STRIP="${TOOLCHAIN_BIN}/aarch64-openwrt-linux-musl-strip"
PO2LMO="${STAGING_HOST}/po2lmo"

echo "=== 1. 编译独立静态二进制 (aarch64-static) ==="
mkdir -p "${DIST_DIR}/bin"
export STAGING_DIR="${IM_DIR}/staging_dir"
${TARGET_CC} -static -Os \
    -I"${REPO_DIR}/include" \
    "${REPO_DIR}/src"/*.c \
    -lm -o "${DIST_DIR}/bin/athena-led-c-static"
${STRIP} "${DIST_DIR}/bin/athena-led-c-static"
file "${DIST_DIR}/bin/athena-led-c-static"

echo "=== 2. 编译动态二进制 (aarch64-dynamic) ==="
${TARGET_CC} -Os \
    -I"${REPO_DIR}/include" \
    "${REPO_DIR}/src"/*.c \
    -lm -o "${DIST_DIR}/bin/athena-led-c"
${STRIP} "${DIST_DIR}/bin/athena-led-c"
file "${DIST_DIR}/bin/athena-led-c"

echo "=== 3. 编译语言包 (.lmo) ==="
mkdir -p "${DIST_DIR}/i18n"
${PO2LMO} "${REPO_DIR}/luci-app-athena-led-c/po/zh_Hans/athena_led_c.po" "${DIST_DIR}/i18n/athena_led_c.zh-cn.lmo"
cp -f "${DIST_DIR}/i18n/athena_led_c.zh-cn.lmo" "${DIST_DIR}/i18n/athena_led_c.zh_Hans.lmo"

echo "=== 4. 拷贝 ImmortalWrt 编译出的 APK 安装包 ==="
mkdir -p "${DIST_DIR}/apk"
find "${IM_DIR}/bin/packages/aarch64_cortex-a53" -name "athena-led-c-*.apk" -exec cp -f {} "${DIST_DIR}/apk/athena-led-c-${VERSION}.apk" \; 2>/dev/null || true
find "${IM_DIR}/bin/packages/aarch64_cortex-a53" -name "luci-app-athena-led-c-*.apk" -exec cp -f {} "${DIST_DIR}/apk/luci-app-athena-led-c-${VERSION}.apk" \; 2>/dev/null || true

echo "=== 5. 构建通用 IPK 安装包 ==="
mkdir -p "${DIST_DIR}/ipk"

# 5.1 构建 athena-led-c.ipk
TMP_IPK="/tmp/ipk-build-daemon"
rm -rf "${TMP_IPK}"
mkdir -p "${TMP_IPK}/data/usr/bin" "${TMP_IPK}/data/etc/init.d" "${TMP_IPK}/data/etc/config" "${TMP_IPK}/control"
cp -f "${DIST_DIR}/bin/athena-led-c-static" "${TMP_IPK}/data/usr/bin/athena-led-c"
chmod 755 "${TMP_IPK}/data/usr/bin/athena-led-c"
cp -f "${REPO_DIR}/athena-led-c/files/athena-led-c.init" "${TMP_IPK}/data/etc/init.d/athena-led-c"
chmod 755 "${TMP_IPK}/data/etc/init.d/athena-led-c"
cp -f "${REPO_DIR}/athena-led-c/files/athena-led-c.config" "${TMP_IPK}/data/etc/config/athena-led-c"
chmod 644 "${TMP_IPK}/data/etc/config/athena-led-c"
mkdir -p "${TMP_IPK}/data/etc/rc.button"
cp -f "${REPO_DIR}/athena-led-c/files/screen.button" "${TMP_IPK}/data/etc/rc.button/screen"
chmod 755 "${TMP_IPK}/data/etc/rc.button/screen"
ln -sf screen "${TMP_IPK}/data/etc/rc.button/BTN_1"
ln -sf screen "${TMP_IPK}/data/etc/rc.button/btn_1"

cat << CPKG > "${TMP_IPK}/control/control"
Package: athena-led-c
Version: ${VERSION}
Depends: libc
Section: utils
Architecture: aarch64_cortex-a53
Maintainer: perlyz
Description: Daemon driving the JDCloud AX6600 (RE-CS-02) front 27x5 LED matrix via TM1628A over GPIO.
CPKG

cat << 'PST' > "${TMP_IPK}/control/postinst"
#!/bin/sh
if [ -z "${IPKG_INSTROOT}" ]; then
	/etc/init.d/athena-led-c enable 2>/dev/null || true
fi
exit 0
PST
chmod 755 "${TMP_IPK}/control/postinst"

echo "2.0" > "${TMP_IPK}/debian-binary"
tar -C "${TMP_IPK}/control" -czf "${TMP_IPK}/control.tar.gz" .
tar -C "${TMP_IPK}/data" -czf "${TMP_IPK}/data.tar.gz" .
tar -C "${TMP_IPK}" -czf "${DIST_DIR}/ipk/athena-led-c_${VERSION}_aarch64_cortex-a53.ipk" debian-binary control.tar.gz data.tar.gz
rm -rf "${TMP_IPK}"

# 5.2 构建 luci-app-athena-led-c.ipk
TMP_IPK="/tmp/ipk-build-luci"
rm -rf "${TMP_IPK}"
mkdir -p "${TMP_IPK}/data/www/luci-static/resources/view/athena_led_c" \
         "${TMP_IPK}/data/usr/share/luci/menu.d" \
         "${TMP_IPK}/data/usr/share/rpcd/acl.d" \
         "${TMP_IPK}/data/usr/lib/lua/luci/i18n" \
         "${TMP_IPK}/control"

cp -f "${REPO_DIR}/luci-app-athena-led-c/htdocs/luci-static/resources/view/athena_led_c/settings.js" \
      "${TMP_IPK}/data/www/luci-static/resources/view/athena_led_c/"
cp -f "${REPO_DIR}/luci-app-athena-led-c/root/usr/share/luci/menu.d/luci-app-athena-led-c.json" \
      "${TMP_IPK}/data/usr/share/luci/menu.d/"
cp -f "${REPO_DIR}/luci-app-athena-led-c/root/usr/share/rpcd/acl.d/luci-app-athena-led-c.json" \
      "${TMP_IPK}/data/usr/share/rpcd/acl.d/"
cp -f "${DIST_DIR}/i18n/athena_led_c.zh-cn.lmo" "${TMP_IPK}/data/usr/lib/lua/luci/i18n/"
cp -f "${DIST_DIR}/i18n/athena_led_c.zh_Hans.lmo" "${TMP_IPK}/data/usr/lib/lua/luci/i18n/"

cat << CPKG > "${TMP_IPK}/control/control"
Package: luci-app-athena-led-c
Version: ${VERSION}
Depends: athena-led-c, luci-base
Section: luci
Architecture: all
Maintainer: perlyz
Description: LuCI Web UI for JDCloud AX6600 (Athena) LED screen controller.
CPKG

cat << 'PST' > "${TMP_IPK}/control/postinst"
#!/bin/sh
if [ -z "${IPKG_INSTROOT}" ]; then
	rm -rf /tmp/luci-indexcache /tmp/luci-modulecache/* 2>/dev/null
	/etc/init.d/rpcd reload >/dev/null 2>&1
fi
exit 0
PST
chmod 755 "${TMP_IPK}/control/postinst"

echo "2.0" > "${TMP_IPK}/debian-binary"
tar -C "${TMP_IPK}/control" -czf "${TMP_IPK}/control.tar.gz" .
tar -C "${TMP_IPK}/data" -czf "${TMP_IPK}/data.tar.gz" .
tar -C "${TMP_IPK}" -czf "${DIST_DIR}/ipk/luci-app-athena-led-c_${VERSION}_all.ipk" debian-binary control.tar.gz data.tar.gz
rm -rf "${TMP_IPK}"

echo "=== 6. 构建直接解压全量包 (athena-led-c-bundle.tar.gz) ==="
TMP_BUNDLE="/tmp/bundle-build"
rm -rf "${TMP_BUNDLE}"
mkdir -p "${TMP_BUNDLE}/usr/bin" \
         "${TMP_BUNDLE}/etc/init.d" \
         "${TMP_BUNDLE}/etc/config" \
         "${TMP_BUNDLE}/www/luci-static/resources/view/athena_led_c" \
         "${TMP_BUNDLE}/usr/share/luci/menu.d" \
         "${TMP_BUNDLE}/usr/share/rpcd/acl.d" \
         "${TMP_BUNDLE}/usr/lib/lua/luci/i18n"

cp -f "${DIST_DIR}/bin/athena-led-c-static" "${TMP_BUNDLE}/usr/bin/athena-led-c"
chmod 755 "${TMP_BUNDLE}/usr/bin/athena-led-c"
cp -f "${REPO_DIR}/athena-led-c/files/athena-led-c.init" "${TMP_BUNDLE}/etc/init.d/athena-led-c"
chmod 755 "${TMP_BUNDLE}/etc/init.d/athena-led-c"
cp -f "${REPO_DIR}/athena-led-c/files/athena-led-c.config" "${TMP_BUNDLE}/etc/config/athena-led-c"
chmod 644 "${TMP_BUNDLE}/etc/config/athena-led-c"
mkdir -p "${TMP_BUNDLE}/etc/rc.button"
cp -f "${REPO_DIR}/athena-led-c/files/screen.button" "${TMP_BUNDLE}/etc/rc.button/screen"
chmod 755 "${TMP_BUNDLE}/etc/rc.button/screen"
ln -sf screen "${TMP_BUNDLE}/etc/rc.button/BTN_1"
ln -sf screen "${TMP_BUNDLE}/etc/rc.button/btn_1"
cp -f "${REPO_DIR}/luci-app-athena-led-c/htdocs/luci-static/resources/view/athena_led_c/settings.js" \
      "${TMP_BUNDLE}/www/luci-static/resources/view/athena_led_c/"
cp -f "${REPO_DIR}/luci-app-athena-led-c/root/usr/share/luci/menu.d/luci-app-athena-led-c.json" \
      "${TMP_BUNDLE}/usr/share/luci/menu.d/"
cp -f "${REPO_DIR}/luci-app-athena-led-c/root/usr/share/rpcd/acl.d/luci-app-athena-led-c.json" \
      "${TMP_BUNDLE}/usr/share/rpcd/acl.d/"
cp -f "${DIST_DIR}/i18n/athena_led_c.zh-cn.lmo" "${TMP_BUNDLE}/usr/lib/lua/luci/i18n/"
cp -f "${DIST_DIR}/i18n/athena_led_c.zh_Hans.lmo" "${TMP_BUNDLE}/usr/lib/lua/luci/i18n/"

tar -C "${TMP_BUNDLE}" -czf "${DIST_DIR}/athena-led-c-bundle.tar.gz" .
rm -rf "${TMP_BUNDLE}"

echo "=== 打包完成！生成产物列表：==="
ls -lh "${DIST_DIR}" "${DIST_DIR}/bin" "${DIST_DIR}/ipk" "${DIST_DIR}/apk"
