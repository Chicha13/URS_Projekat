TMD3725_USERSPACE_DRIVER_VERSION = 1.0
TMD3725_USERSPACE_DRIVER_SITE = $(BR2_EXTERNAL_TMD3725_PATH)/package/tmd3725-userspace-driver/src
TMD3725_USERSPACE_DRIVER_SITE_METHOD = local


define TMD3725_USERSPACE_DRIVER_BUILD_CMDS
    $(MAKE) CC="$(TARGET_CC)" LD="$(TARGET_LD)" -C $(@D) all
endef

define TMD3725_USERSPACE_DRIVER_INSTALL_TARGET_CMDS
    $(INSTALL) -D -m 0755 $(@D)/tmd3725_sensor \
        $(TARGET_DIR)/usr/bin/tmd3725_sensor
    $(INSTALL) -D -m 0644 $(@D)/tmd3725.conf \
        $(TARGET_DIR)/etc/tmd3725.conf
endef

$(eval $(generic-package))
