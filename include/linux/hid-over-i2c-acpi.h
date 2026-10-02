/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _HID_OVER_I2C_ACPI_H_
#define _HID_OVER_I2C_ACPI_H_

#include <linux/acpi.h>
#include <linux/uuid.h>

#ifdef CONFIG_ACPI
static inline union acpi_object *i2c_hid_acpi_evaluate_dsm(acpi_handle handle)
{
	/* HID I²C Device: 3cdff6f7-4267-4555-ad05-b30a3d8938de */
	static const guid_t i2c_hid_guid =
		GUID_INIT(0x3CDFF6F7, 0x4267, 0x4555,
			  0xAD, 0x05, 0xB3, 0x0A, 0x3D, 0x89, 0x38, 0xDE);

	return acpi_evaluate_dsm_typed(handle, &i2c_hid_guid,
				       1, 1, NULL, ACPI_TYPE_INTEGER);
}

static inline int i2c_hid_acpi_get_descriptor(struct acpi_device *adev)
{
	acpi_handle handle = acpi_device_handle(adev);
	union acpi_object *obj;
	u16 addr;

	obj = i2c_hid_acpi_evaluate_dsm(handle);
	if (!obj) {
		acpi_handle_err(handle,
				"Error _DSM call to get HID descriptor address failed\n");
		return -ENODEV;
	}

	addr = obj->integer.value;
	ACPI_FREE(obj);
	return addr;
}

/**
 * i2c_hid_acpi_is_hid_device - check whether ACPI describes a device as I2C-HID
 * @dev: device to check
 *
 * Native drivers of devices which can also be driven by i2c-hid can use this
 * to avoid binding to devices which the firmware describes as I2C-HID
 * compatible.
 *
 * Returns: %true if @dev has an I2C-HID compatible ACPI ID and a working
 * I2C-HID _DSM, %false otherwise.
 */
static inline bool i2c_hid_acpi_is_hid_device(struct device *dev)
{
	static const struct acpi_device_id i2c_hid_ids[] = {
		{ "ACPI0C50" },
		{ "PNP0C50" },
		{ }
	};
	struct acpi_device *adev = ACPI_COMPANION(dev);
	union acpi_object *obj;

	if (!adev || acpi_match_device_ids(adev, i2c_hid_ids))
		return false;

	obj = i2c_hid_acpi_evaluate_dsm(acpi_device_handle(adev));
	if (!obj)
		return false;

	ACPI_FREE(obj);
	return true;
}
#else
static inline bool i2c_hid_acpi_is_hid_device(struct device *dev)
{
	return false;
}
#endif

#endif
