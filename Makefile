include $(TOPDIR)/rules.mk

PKG_NAME:=ur-wireless-tools
PKG_VERSION:=1.0.0
PKG_RELEASE:=1

include $(INCLUDE_DIR)/package.mk

define Package/ur-wireless-tools
  SECTION:=net
  CATEGORY:=Network
  TITLE:= $(PKG_NAME)
  DEPENDS:= +coreutils +wireless-tools +iw +hostapd +wpa-supplicant

  MAINTAINER:=Fehmi Yousfi <fehmi_yousfi@hotmail.com>Z
endef

define Package/ur-wireless-tools/description
	set of wireless interface tools for automated Wireless system monitor on ultima os rpc 
endef

define Build/Configure
    # Empty body to skip the configure step
endef

define Build/Prepare
	if [ ! -d $(PKG_BUILD_DIR) ]; then \
		mkdir -p $(PKG_BUILD_DIR); \
	elif [ -n "$(ls -A $(PKG_BUILD_DIR))" ]; then \
		rm -r $(PKG_BUILD_DIR); \
		mkdir -p $(PKG_BUILD_DIR); \
	else \
		rm -r $(PKG_BUILD_DIR); \
		mkdir -p $(PKG_BUILD_DIR); \
	fi
	$(CP) ./pkg_src* $(PKG_BUILD_DIR)
endef

define Build/Compile
	echo "Building $(PKG_NAME) $(PKG_VERSION) in $(PKG_BUILD_DIR)"
	$(MAKE) -C $(PKG_BUILD_DIR)/pkg_src CC=$(TARGET_CC) CXX=$(TARGET_CXX)
	cp $(PKG_BUILD_DIR)/pkg_src/build/ur-wireless-tools $(PKG_BUILD_DIR)/$(PKG_NAME)
endef

define Package/ur-wireless-tools/install
	$(INSTALL_DIR) $(1)/bin/
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/$(PKG_NAME) $(1)/bin/$(PKG_NAME)
endef

define Build/clean
	rm -rf $(PKG_BUILD_DIR)
endef

$(eval $(call BuildPackage,ur-wireless-tools))
