# OpenWrt package definition for athena-led-c.
#
# Usage from an OpenWrt buildroot:
#   ln -s /data00/repos/JDC-AX6600-Athena-LED-Controller-C/openwrt package/athena-led-c
#   make package/athena-led-c/compile V=s
#
# The plain top-level Makefile of this repository is intentionally reused for
# the actual build so the PC and OpenWrt build flows can never drift apart.

include $(TOPDIR)/rules.mk

PKG_NAME:=athena-led-c
PKG_VERSION:=0.2.0
PKG_RELEASE:=1
PKG_LICENSE:=Apache-2.0
PKG_MAINTAINER:=perlyz

PKG_BUILD_DIR:=$(BUILD_DIR)/$(PKG_NAME)-$(PKG_VERSION)
PKG_BUILD_DEPENDS:=

include $(INCLUDE_DIR)/package.mk

define Package/athena-led-c
  SECTION:=utils
  CATEGORY:=Utilities
  TITLE:=JDCloud AX6600 Athena LED matrix screen daemon
  DEPENDS:=@(aarch64||arm) +librt
endef

define Package/athena-led-c/description
Pure C11 daemon driving the JDCloud AX6600 (RE-CS-02) front 27x5
LED matrix via TM1628A over GPIO. Local-only features: clock/date,
Chinese lunar calendar, sunrise/sunset, CPU/memory/temperature and
per-interface throughput. Terminal simulator included (--sim).
endef

# Repo root locator: works both when this directory is symlinked into
# package/ (sources live in ../) and when the whole repo is used as the
# package dir (sources live in ./). Note: ./Makefile cannot be probed
# directly because it exists in BOTH layouts (here it is the package file).
REPO_ROOT := $(if $(wildcard ./src),.,..)

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) $(REPO_ROOT)/src $(PKG_BUILD_DIR)/
	$(CP) $(REPO_ROOT)/include $(PKG_BUILD_DIR)/
	$(CP) $(REPO_ROOT)/Makefile $(PKG_BUILD_DIR)/
endef

define Build/Compile
	$(MAKE) -C $(PKG_BUILD_DIR) \
		CC="$(TARGET_CC)" \
		CFLAGS="$(TARGET_CFLAGS)" \
		LDFLAGS="$(TARGET_LDFLAGS)"
endef

define Package/athena-led-c/install
	$(INSTALL_DIR) $(1)/usr/bin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/athena-led-c $(1)/usr/bin/
	$(INSTALL_DIR) $(1)/etc/init.d
	$(INSTALL_BIN) ./files/athena-led-c.init $(1)/etc/init.d/athena-led-c
	$(INSTALL_DIR) $(1)/etc/config
	$(INSTALL_CONF) ./files/athena-led-c.config $(1)/etc/config/athena-led-c
endef

$(eval $(call BuildPackage,athena-led-c))
