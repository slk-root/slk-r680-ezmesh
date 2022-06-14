include $(TOPDIR)/rules.mk

PKG_NAME:=slkmac2
PKG_VERSION:=1.1
PKG_BUILD_DIR:= $(BUILD_DIR)/$(PKG_NAME)


include $(INCLUDE_DIR)/package.mk
include $(INCLUDE_DIR)/nls.mk

define Package/slkmac2
  CATEGORY:=Examples
  TITLE:=slkmac2 
  DEPENDS:=
endef

define Package/slkmac2/description
	If you can't figure out what this program does, you're probably  
	brain-dead and need immediate medical attention.
endef

define Build/Prepare
	mkdir -p $(PKG_BUILD_DIR)
	$(CP) ./src/* $(PKG_BUILD_DIR)/
endef

define Package/slkmac2/install
	$(INSTALL_DIR) $(1)/usr/sbin
	$(INSTALL_BIN) $(PKG_BUILD_DIR)/slkmac2 $(1)/usr/sbin/
endef


$(eval $(call BuildPackage,slkmac2))
