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
#define DRIVER_VERSION		"1.5"

#include <linux/module.h>   /* Needed by all modules */
#include <linux/types.h>
#include <linux/io.h>
#include <linux/mm.h>
#include "generic_access.h"

/* Lock to protect access to the hardware within the any kernel driver.
 * Note that this does not protect against accesses from userspace, but ideally
 * there is a clear responsability split between userspace access and
 * kernelspace access.
 */
DEFINE_SPINLOCK(ga_spinlock);
DEFINE_MUTEX(ga_mutex);

/******************************************************************************/
struct ga_ioaddr_info {
	phys_addr_t		addr;
	phys_addr_t		size;
	void			*opaque;
	struct ga_ioaddr_ops	ops;
};

static struct ga_ioaddr_info addr_info;
static int ga_lock_mode = GA_LM_SPINLOCK;

/******************************************************************************/
void ga_set_lock_mode(int mode)
{
	ga_lock_mode = mode;
}
EXPORT_SYMBOL_GPL(ga_set_lock_mode);

/******************************************************************************/
int ga_set_phys_addr_ops(phys_addr_t addr, phys_addr_t size, struct ga_ioaddr_ops *ops, void *opaque)
{
	if (ops == NULL) {
		memset(&addr_info, 0, sizeof(addr_info));
		return 0;
	}

	addr_info.addr = addr;
	addr_info.size = size;
	addr_info.opaque = opaque;
	addr_info.ops = *ops;

	/* Call init function (if any) */
	if (addr_info.ops.init != NULL) {
		int ret = addr_info.ops.init(addr, size, opaque);
		if (ret) {
			memset(&addr_info, 0, sizeof(addr_info));
			return ret;
		}
	}
	return 0;
}
EXPORT_SYMBOL_GPL(ga_set_phys_addr_ops);

/******************************************************************************/
static inline int ga_ioaddr_to_phys(const void __iomem *ioaddr, phys_addr_t *paddr)
{
	struct page	*page;
	phys_addr_t	addr;

	page = vmalloc_to_page(ioaddr);
	if (page == NULL)
		return -EFAULT; /* not a ioremap'ed address */

	addr = page_to_phys(page) + offset_in_page(ioaddr);

	if ((addr >= addr_info.addr) && (addr < addr_info.addr + addr_info.size)) {
		*paddr = addr;
		return 1; /* we are in range */
	}

	return 0; /* we are not in range */
}

/******************************************************************************/
static u8 ga_raw_readb(const void __iomem *ioaddr)
{
	if (addr_info.ops.read8 != NULL) {
		phys_addr_t addr;
		int ret = ga_ioaddr_to_phys(ioaddr, &addr);
		if (ret < 0)
			return 0; /* error */
		else if (ret > 0)
			return addr_info.ops.read8(addr, addr_info.opaque); /* address match */
	}
	/* No callback or no address match */
	return __raw_readb(ioaddr);
}

/******************************************************************************/
static void ga_raw_writeb(u8 val, void __iomem *ioaddr)
{
	if (addr_info.ops.write8 != NULL) {
		phys_addr_t addr;
		int ret = ga_ioaddr_to_phys(ioaddr, &addr);
		if (ret < 0)
			return; /* error */
		else if (ret > 0)
			return addr_info.ops.write8(val, addr, addr_info.opaque); /* address match */
	}
	/* No callback or no address match */
	__raw_writeb(val, ioaddr);
}

/******************************************************************************/
#define LOCK										\
	unsigned long flags = 0;							\
	if (ga_lock_mode == GA_LM_SPINLOCK)						\
		spin_lock_irqsave(&ga_spinlock, flags);					\
	else										\
		mutex_lock(&ga_mutex);

#define UNLOCK										\
	if (ga_lock_mode == GA_LM_SPINLOCK)						\
		spin_unlock_irqrestore(&ga_spinlock, flags);				\
	else										\
		mutex_unlock(&ga_mutex);

/******************************************************************************
 * Writing
 ******************************************************************************/
#define GA_DEFINE_REG_WRITE(__f,__T,__r,__w,__a)					\
int __f(void __iomem *addr, __T mask, __T value, int options)				\
{											\
	int modified = GA_MODIFIED_NONE;						\
	LOCK;										\
	if ((options & GA_WRITE) || (mask == __a)) {					\
		/* We are updating all bits so no need to read first */			\
		__w(value & mask, addr);						\
		modified = GA_MODIFIED_MAYBE; /* we are not sure we are modifying */	\
	}										\
	else if (options & (GA_READ_WRITE_CONDITIONAL|GA_READ_WRITE_ALWAYS)) {		\
		/* We are updating specific bits so read first */			\
		__T old_reg, new_reg;							\
		new_reg = old_reg = __r(addr);						\
		new_reg &= ~mask;							\
		new_reg |= (value & mask);						\
		if (old_reg != new_reg)							\
			modified = GA_MODIFIED_SURE; /* we are sure we are modifying */	\
		/* Only write when we need to write */					\
		if ((options & GA_READ_WRITE_ALWAYS) || (old_reg != new_reg))		\
			__w(new_reg, addr);						\
	}										\
	UNLOCK;										\
	return modified;								\
}											\
EXPORT_SYMBOL_GPL(__f);

/******************************************************************************/
GA_DEFINE_REG_WRITE(ga_reg_write8,  u8,  ga_raw_readb, ga_raw_writeb, 0xff)
GA_DEFINE_REG_WRITE(ga_reg_write16, u16, __raw_readw,  __raw_writew,  0xffff)
GA_DEFINE_REG_WRITE(ga_reg_write32, u32, __raw_readl,  __raw_writel,  0xffffffffU)

#ifdef CONFIG_64BIT
GA_DEFINE_REG_WRITE(ga_reg_write64, u64, __raw_readq,  __raw_writeq,  0xffffffffffffffffULL)
#else
int ga_reg_write64(void __iomem *addr, u64 mask, u64 value, int options)
{
#ifdef __LITTLE_ENDIAN
	/* Least significant first */
	int rc1 = ga_reg_write32(addr, (u32)(mask & 0x00000000ffffffffULL), (u32)(value & 0x00000000ffffffffU), options);
	int rc2 = ga_reg_write32(addr + 4, (u32)(mask >> 32), (u32)(value >> 32), options);
#else
	/* Most significant first */
	int rc1 = ga_reg_write32(addr, (u32)(mask >> 32), (u32)(value >> 32), options);
	int rc2 = ga_reg_write32(addr + 4, (u32)(mask & 0x00000000ffffffffULL), (u32)(value & 0x00000000ffffffffU), options);
#endif
	if ((rc1 == GA_MODIFIED_SURE) || (rc2 == GA_MODIFIED_SURE))
		return GA_MODIFIED_SURE;
	else if ((rc1 == GA_MODIFIED_MAYBE) || (rc2 == GA_MODIFIED_MAYBE))
		return GA_MODIFIED_MAYBE;
	else
		return GA_MODIFIED_NONE;
}
#endif

/******************************************************************************/
int ga_reg_write(void __iomem *addr, int access_size, u64 mask, u64 value, int options)
{
	/* Handle write operation */
	switch (access_size) {
	case 8:
		ga_reg_write8(addr, (u8)mask, (u8)value, options);
		break;
	case 16:
		ga_reg_write16(addr, (u16)mask, (u16)value, options);
		break;
	case 32:
		ga_reg_write32(addr, (u32)mask, (u32)value, options);
		break;
	case 64:
		ga_reg_write64(addr, (u64)mask, (u64)value, options);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(ga_reg_write);

/******************************************************************************
 * Reading
 ******************************************************************************/
#define GA_DEFINE_REG_READ(__f,__T,__r)							\
__T __f(void __iomem *addr, __T mask)							\
{											\
	__T reg;									\
	LOCK;										\
	reg = __r(addr) & mask;								\
	UNLOCK;										\
	return reg;									\
}											\
EXPORT_SYMBOL_GPL(__f);

/******************************************************************************/
GA_DEFINE_REG_READ(ga_reg_read8,  u8,  ga_raw_readb)
GA_DEFINE_REG_READ(ga_reg_read16, u16, __raw_readw)
GA_DEFINE_REG_READ(ga_reg_read32, u32, __raw_readl)

#ifdef CONFIG_64BIT
GA_DEFINE_REG_READ(ga_reg_read64, u64, __raw_readq)
#else
u64 ga_reg_read64(void __iomem *addr, u64 mask)
{
#ifdef __LITTLE_ENDIAN
	/* Least significant first */
	u64 lsv = (u64)ga_reg_read32(addr, (u32)(mask & 0x00000000ffffffffULL));
	u64 msv = (u64)ga_reg_read32(addr + 4, (u32)(mask >> 32));
#else
	/* Most significant first */
	u64 msv = (u64)ga_reg_read32(addr, (u32)(mask >> 32));
	u64 lsv = (u64)ga_reg_read32(addr + 4, (u32)(mask & 0x00000000ffffffffULL));
#endif
	return (msv << 32) & lsv;
}
#endif

/******************************************************************************/
int ga_reg_read(void __iomem *addr, int access_size, u64 mask, u64 *value)
{
	/* Handle read operation */
	switch (access_size) {
	case 8:
		*value = (u64)ga_reg_read8(addr, (u8)mask);
		break;
	case 16:
		*value = (u64)ga_reg_read16(addr, (u16)mask);
		break;
	case 32:
		*value = (u64)ga_reg_read32(addr, (u32)mask);
		break;
	case 64:
		*value = (u64)ga_reg_read64(addr, (u64)mask);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(ga_reg_read);

/******************************************************************************/
MODULE_AUTHOR("babytech@126.com");
MODULE_DESCRIPTION("Generic access driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("generic_access");
MODULE_VERSION(DRIVER_VERSION);
