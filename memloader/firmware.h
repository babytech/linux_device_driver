/******************************************************************************
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 *
*******************************************************************************/
#ifndef KERNEL_FIRMWARE_H
#define KERNEL_FIRMWARE_H

#include <linux/firmware.h>

struct fw_context {
/* Public members */
	int (*start)(const struct fw_context *);	/* Start of write operation. Return: >=0=OK,<0=Error */
	int (*write)(const struct fw_context *);	/* Write operation. Return: >0=nr bytes written,0=EOF,<0=Error */
	int (*read)(const struct fw_context *);		/* Read operation. Return: >=0=OK,<0=Error */
	int (*end)(const struct fw_context *, int);	/* End of write operation. Return: >=0=OK,<0=Error */
	void			*opaque;	/* Opaque data for above callbacks */
	const struct firmware *fw; /* returned by 'request_firmware()', more detail info in it */
	const unsigned char	*data; /* .data = fw->data; Data to write (to be indexed by current_count) */
	int			total_count;	/* Total bytes to write */
	int			current_count;	/* Current byte to write */
	int			devicemask;	/* Which devices do I need to load */
	int			blockread;	/* Number of bytes to write before doing a read (configurable) */
	int			blocksize;	/* Number of bytes to write before doing a usleep (configurable) */
	int			blockdelay;	/* Delay in us between block writes (configurable) */
/* Private members */
	struct device		*dev;		/* Device */
	struct kobject		kobj;		/* Firmware sysfs attributes */
	char			filename[32];	/* Filename which contains the data (configurable) */
	unsigned int		state:8;	/* Current state of the firmware */
	unsigned int		kobj_init:2;	/* True of kobj objects were initialized */
	unsigned int		kobj_added:2;	/* True of kobj objects were added successfully */
};

struct device;

int fw_initialize(struct fw_context *ctx, struct device *dev_configuration, struct device *dev_target);
void fw_cleanup(struct fw_context *ctx);

#endif
