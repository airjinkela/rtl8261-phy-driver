include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

PKG_NAME:=rtl8621
PKG_VERSION:=0.0.1
PKG_RELEASE:=1
PKG_MAINTAINER:=StarField Xu (air_jinkela@163.com)

include $(INCLUDE_DIR)/package.mk

define KernelPackage/$(PKG_NAME)
  SUBMENU:=Other modules
  TITLE:=$(PKG_NAME)
  FILES:=$(PKG_BUILD_DIR)/rtl8261.ko
  AUTOLOAD:=$(call AutoLoad,99,rtl8261)
endef

define Build/Compile
	+$(KERNEL_MAKE) $(PKG_JOBS) \
		M="$(PKG_BUILD_DIR)" \
		CONFIG_REALTEK_PHY_RTL8261D=m \
		modules
endef

$(eval $(call KernelPackage,$(PKG_NAME)))
