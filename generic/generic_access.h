/******************************************************************************
 * Copyright (c) 2015 Alcatel-Lucent
 * All rights reserved.
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
/******************************************************************************
**                                                                           **
**                           ALCATEL-LUCENT                                  **
**                                                                           **
*******************************************************************************/

/*************************** IDENTIFICATION ***********************************
**
** Project       : REBORN
**
** File name     : generic_access.h
**
** Description   : Interface to the generic access driver
**
** Contact       : Tom Philips (tom.philips@nokia.com)
**
** Creation Date : 28.11.2014
**
*******************************************************************************/
#ifndef KERNEL_GENERIC_ACCESS_H
#define KERNEL_GENERIC_ACCESS_H

#include <linux/types.h>

/* Options of ga_reg_writeX */
#define GA_WRITE			0x01 /* Write operation: only write, do not read first */
#define GA_READ_WRITE_CONDITIONAL	0x02 /* Read/update/write operation: read first, update, only write when modified */
#define GA_READ_WRITE_ALWAYS		0x04 /* Read/update/write operation: read first, update, always write (even if not modified) */
#define GA_WRITE_DEFAULT		GA_READ_WRITE_CONDITIONAL
/* Note that GA_READ_WRITE_XXX are demoted to GA_WRITE when all bits in 'mask' are set! */

/* Return codes of ga_reg_writeX */
#define GA_MODIFIED_NONE	0 /* The written value was the same from what was read or nothing was written if GA_READ_WRITE_CONDITIONAL was specified */
#define GA_MODIFIED_SURE	1 /* The written value was different from what was read */
#define GA_MODIFIED_MAYBE	2 /* Only a write was done (no read) so we are not sure that we modified the register */

/* Write a 8, 16, 32 or 64 bit register.
 * 
 * If GA_WRITE is specified or if all bits are set in 'mask', the register is only written with 'value' (no read first).
 * 
 * Otherwise, the register is first read, then updated with the bits in 'value', only affecting the bits in 'mask', 
 * and then the result is written back to the register.
 * If GA_READ_WRITE_ALWAYS is specified, the result is always written.
 * If GA_READ_WRITE_CONDITIONAL is specified, the result is only written when the result differs with what was read. */
int ga_reg_write8(void __iomem *addr, u8 mask, u8 value, int options);
int ga_reg_write16(void __iomem *addr, u16 mask, u16 value, int options);
int ga_reg_write32(void __iomem *addr, u32 mask, u32 value, int options);
int ga_reg_write64(void __iomem *addr, u64 mask, u64 value, int options);
int ga_reg_write(void __iomem *addr, int access_size, u64 mask, u64 value, int options);

/* Read a 8, 16, 32 or 64 bit register.
 */
u8 ga_reg_read8(void __iomem *addr, u8 mask);
u16 ga_reg_read16(void __iomem *addr, u16 mask);
u32 ga_reg_read32(void __iomem *addr, u32 mask);
u64 ga_reg_read64(void __iomem *addr, u64 mask);
int ga_reg_read(void __iomem *addr, int access_size, u64 mask, u64 *value);

/* Hooks for reading and writing to a memory location.
 * For the moment only 8-bit accesses can be hooked into.
 */
struct ga_ioaddr_ops {
	int (*init)(phys_addr_t addr, phys_addr_t size, void *opaque);
	u8 (*read8)(phys_addr_t addr, void *opaque);
	void (*write8)(u8 val, phys_addr_t addr, void *opaque);
};

int ga_set_phys_addr_ops(phys_addr_t addr, phys_addr_t size, struct ga_ioaddr_ops *ops, void *opaque);

#define GA_LM_SPINLOCK	0
#define GA_LM_MUTEX	1
void ga_set_lock_mode(int mode);

#endif
