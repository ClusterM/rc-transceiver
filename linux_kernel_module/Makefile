include $(TOPDIR)/rules.mk
include $(INCLUDE_DIR)/kernel.mk

# name
PKG_NAME:=rc-transceiver
# version of what we are downloading
PKG_VERSION:=1.0
# version of this makefile
PKG_RELEASE:=0

REMOTE_DIR:=root@10.13.1.43:/lib/modules/4.14.81/

PKG_BUILD_DIR:=$(KERNEL_BUILD_DIR)/$(PKG_NAME)
#PKG_BUILD_DIR:=$(TOPDIR)/$(PKG_NAME)
PKG_CHECK_FORMAT_SECURITY:=0

include $(INCLUDE_DIR)/package.mk

define KernelPackage/$(PKG_NAME)
	SUBMENU:=Other modules
	TITLE:=RC-transceiver driver
	FILES:= $(PKG_BUILD_DIR)/rc-transceiver.ko
endef

define KernelPackage/$(PKG_NAME)/description
	RC-transceiver driver.
endef

MAKE_OPTS:= \
	$(KERNEL_MAKE_FLAGS) \
	SUBDIRS=$(PKG_BUILD_DIR)

define Build/Compile
	$(MAKE) -C "$(LINUX_DIR)" \
	    $(MAKE_OPTS) \
	    modules
	scp $(PKG_BUILD_DIR)/$(PKG_NAME).ko $(REMOTE_DIR)
endef

$(eval $(call KernelPackage,$(PKG_NAME)))
