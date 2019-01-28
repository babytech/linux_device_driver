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
*******************************************************************************/
#ifndef __DEV_COMMON_H__
#define __DEV_COMMON_H__

#include <linux/types.h>
#include "generic_access.h"

/* Redefine to even lower level atomic accesses (protected by a spinlock) */
#define dev_read(addr)					ga_reg_read8(addr, 0xff)
#define dev_read8(addr)				ga_reg_read8(addr, 0xff)
#define dev_read16(addr)				ga_reg_read16(addr, 0xffff)
#define dev_write(addr, val)				ga_reg_write8(addr, 0xff, val, GA_WRITE)
#define dev_write8(addr, val)				ga_reg_write8(addr, 0xff, val, GA_WRITE)
#define dev_write16(addr, val)				ga_reg_write16(addr, 0xffff, val, GA_WRITE)
/* Read from addr, disable bits in mask, update bits in upd and write to addr */
#define dev_read_modify_write(addr, mask, upd)		ga_reg_write8(addr, mask, upd, GA_READ_WRITE_ALWAYS) /* forced write */
#define dev_read_modify_write_cond(addr, mask, upd)	ga_reg_write8(addr, mask, upd, GA_READ_WRITE_CONDITIONAL) /* conditional write */

#endif /* __DEV_COMMON_H__ */
