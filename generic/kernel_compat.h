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
#ifndef KERNEL_COMPAT_H
#define KERNEL_COMPAT_H

/******************************************************************************
 * Redefine the definitions used in newer kernels to the old definitions 
 * based on the kernel version.
 ******************************************************************************/

/******************************************************************************/
/* Kernel < 2.6.39 uses set_irq_xxx i.o. irq_set_xxx */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,39))

#define irq_set_chip	set_irq_chip
#define irq_set_handler	set_irq_handler

#define irq_mask	mask
#define irq_disable	disable
#define irq_unmask	unmask
#define irq_ack		ack

#endif /* 2.6.39 */

/******************************************************************************/
/* Kernel >= 2.6.36 does not have an 'owner' field in 'struct attribute' */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36))

#define SYSFS_ATTR(_name,_mode) { \
	.name = __stringify(_name), \
	.owner = THIS_MODULE, \
	.mode = _mode \
}

#else

#define SYSFS_ATTR(_name,_mode) { \
	.name = __stringify(_name), \
	.mode = _mode \
}

#endif /* 2.6.36 */

#endif
