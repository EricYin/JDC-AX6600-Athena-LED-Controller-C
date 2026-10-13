# OpenWrt package definition for athena-led-c.

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
Daemon driving the JDCloud AX6600 (RE-CS-02) front 27x5 LED matrix
via TM1628A over GPIO. Local-only features: clock/date,
Chinese lunar calendar, sunrise/sunset, CPU/memory/temperature and
per-interface throughput. Terminal simulator included (--sim).
endef

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
	$(INSTALL_DIR) $(1)/etc/rc.button
	$(INSTALL_BIN) ./files/screen.button $(1)/etc/rc.button/screen
	ln -sf screen $(1)/etc/rc.button/BTN_1
	ln -sf screen $(1)/etc/rc.button/btn_1
endef

$(eval $(call BuildPackage,athena-led-c))
