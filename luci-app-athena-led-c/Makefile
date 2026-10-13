include $(TOPDIR)/rules.mk

PKG_NAME:=luci-app-athena-led-c
PKG_VERSION:=0.2.0
PKG_RELEASE:=1

PKG_MAINTAINER:=perlyz
PKG_LICENSE:=Apache-2.0

include $(INCLUDE_DIR)/package.mk

define Package/$(PKG_NAME)
	SECTION:=luci
	CATEGORY:=LuCI
	SUBMENU:=3. Applications
	TITLE:=LuCI Support for Athena LED (Pure C daemon)
	DEPENDS:=+athena-led-c +luci-base
	PKGARCH:=all
endef

define Package/$(PKG_NAME)/description
Web UI for the JDCloud AX6600 (Athena) LED screen pure C daemon.
Features 27x5 HTML5 canvas live matrix preview, brightness control,
polling and page duration configuration, and Chinese i18n support.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
endef

define Build/Compile
	[ -d ./po/zh_Hans ] && po2lmo ./po/zh_Hans/athena_led_c.po $(PKG_BUILD_DIR)/zh_Hans.lmo || true
endef

define Package/$(PKG_NAME)/install
	$(INSTALL_DIR) $(1)/www/luci-static/resources/view/athena_led_c
	$(INSTALL_DATA) ./htdocs/luci-static/resources/view/athena_led_c/settings.js \
		$(1)/www/luci-static/resources/view/athena_led_c/

	$(INSTALL_DIR) $(1)/usr/share/luci/menu.d
	$(INSTALL_DATA) ./root/usr/share/luci/menu.d/luci-app-athena-led-c.json \
		$(1)/usr/share/luci/menu.d/

	$(INSTALL_DIR) $(1)/usr/share/rpcd/acl.d
	$(INSTALL_DATA) ./root/usr/share/rpcd/acl.d/luci-app-athena-led-c.json \
		$(1)/usr/share/rpcd/acl.d/

	$(INSTALL_DIR) $(1)/usr/lib/lua/luci/i18n
	[ -f $(PKG_BUILD_DIR)/zh_Hans.lmo ] && { \
		$(INSTALL_DATA) $(PKG_BUILD_DIR)/zh_Hans.lmo $(1)/usr/lib/lua/luci/i18n/athena_led_c.zh-cn.lmo; \
		$(INSTALL_DATA) $(PKG_BUILD_DIR)/zh_Hans.lmo $(1)/usr/lib/lua/luci/i18n/athena_led_c.zh_Hans.lmo; \
	} || true
endef

define Package/$(PKG_NAME)/postinst
#!/bin/sh
if [ -z "$${IPKG_INSTROOT}" ]; then
	rm -rf /tmp/luci-indexcache /tmp/luci-modulecache/* 2>/dev/null
	/etc/init.d/rpcd reload >/dev/null 2>&1
fi
exit 0
endef

$(eval $(call BuildPackage,$(PKG_NAME)))
