#include <linux/module.h>   /* Needed by all modules */
#include <linux/moduleparam.h>
#include <linux/kernel.h>   /* Needed for KERN_INFO */

#include <asm/io.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/of.h>
#include <linux/ioport.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>

static char *buffer_info = "";

#define LOGBUFF_MAGIC       0xc0de4ced
#define LOGBUFF_LEN         (0x8000)
#define LOGBUFF_MASK        (LOGBUFF_LEN-1)
#define LOGBUFF_SIZE        (2048)

typedef struct {
   __u32   tag;
   __u64   start;
   __u64   con;    /* next char to be sent to consoles */
   __u64   end;
   __u64   chars;
   __u8    buf[0];
} logbuff_t;

static void process_logbuffer(void *logbuffer,char *buffer_name,unsigned long size, struct seq_file *m, void *v)
{
	__u64 log_idx,local_idx;
	char *buf = (char *)logbuffer;
	char *local_buffer = 0;

	local_buffer = kmalloc(LOGBUFF_SIZE, GFP_KERNEL);

	if (!local_buffer) {
		seq_printf(m,"kmalloc %d bytes failed - ignore it!\n", LOGBUFF_SIZE);
		return;
	}

	/* The logbuffer consists of strings termintated by a newline character.
	 * The loop below will parse the complete buffer and issue a printk call
	 * for each string terminated by a \n it finds.
	 */
	log_idx = 0;
	local_idx = 0;
	while (log_idx < size) {    //size <= LOGBUFF_LEN
		/* Remove \r characters. */
		if (buf[log_idx & LOGBUFF_MASK] == '\r') {
			log_idx++;
			continue;
		}
		local_buffer[local_idx] = buf[log_idx & LOGBUFF_MASK];
		if (buf[log_idx & LOGBUFF_MASK] == '\n') {
			local_buffer[++local_idx] = 0;
			seq_printf(m,"%s: %s",buffer_name,local_buffer);
			local_idx = 0;
		}
		else {
			local_idx++;
		}
		/* If the line becomes too long, skip the last part. */
		if (local_idx >= LOGBUFF_SIZE-1 ) {
			local_idx--;
		}
		log_idx++;
	}
	kfree(local_buffer);
}

static char* get_mytoken(char *inp,char *outp)
{
	char *cur, *ptr;

	cur = inp;
	ptr = strchr(cur,',');
	if (ptr)
		*ptr = 0;
	strcpy(outp,cur);
	if (ptr)
		cur = ptr+1;
	else
		cur += strlen(outp);

	return cur;
}

static int logbuffer_show(struct seq_file *m, void *v)
{
	char addr[128],length[128];
	char local_buffer_info[512];
	char *cur;
	void *logbuffer = 0;
	char buffer_name[128];
	unsigned long mem_address;
	unsigned long mem_size;
	logbuff_t *log = 0;
	__u64 offset = 0;
	__u64 end = 0;
	unsigned long size = 0;

	strcpy(local_buffer_info,buffer_info);
	cur = local_buffer_info;
	cur = get_mytoken(cur,buffer_name);
	cur = get_mytoken(cur,length);
	cur = get_mytoken(cur,addr);

	mem_address = simple_strtol(addr,NULL,0);
	mem_size = simple_strtol(length,NULL,0);

	if (mem_address && mem_size > sizeof(logbuff_t)) {
		seq_printf(m,"processing buffer at %lx size %lx\n",mem_address,mem_size);

		if (!request_mem_region(mem_address, mem_size, "logbuffer")) {
			seq_printf(m,"logbuffer: request_mem_region on logbuffer failed\n");
			goto err_request_logbuffer;
		}

		logbuffer = ioremap(mem_address,mem_size);
		if (logbuffer == NULL) {
			seq_printf(m,"logbuffer: failed to map logbuffer\n");
			goto err_ioremap_logbuffer;
		}

		log = (logbuff_t*)(logbuffer);

		/* When no properly setup buffer is found, reset pointers */
		seq_printf(m,"Value(%p)=%x magic=%lx\n",log,log->tag,(unsigned long)LOGBUFF_MAGIC);
		if (log->tag != (__u32)LOGBUFF_MAGIC) {
			seq_printf(m,"Properly setup external logbuffer not found - ignore it!\n");
			goto finish;
		}

		seq_printf(m,"Logbuffer at 0x%p, start 0x%llx, end 0x%llx\n", log->buf, log->start, log->end);

		if ((log->start > log->end) || (log->end + sizeof(logbuff_t) > mem_size)) {
			seq_printf(m,"logbuffer start/end out of range, or mem_size incorrect - ignore it!\n");
			goto finish;
		}

		offset = log->start;
		end = log->end;

		while ((offset < end) && (sizeof(logbuff_t) + offset < mem_size)) {
			if(offset + LOGBUFF_LEN > end)
				size = end - offset;
			else
				size = LOGBUFF_LEN;

			process_logbuffer(log->buf+offset,buffer_name,size,m,v);
			offset += size;
		}
	}
	else {
		seq_printf(m,"Invalid input, expects format=name,length,start\n");
		goto out;
	}
/* error handling */
finish:
	iounmap(logbuffer);
err_ioremap_logbuffer:
	if (mem_address != 0)
		release_mem_region(mem_address, mem_size);
err_request_logbuffer:
out:
	return 0;
}

static int logbuffer_open(struct inode *inode, struct file *file)
{
	return single_open(file, logbuffer_show, NULL);
}

static const struct file_operations logbuffer_operations = {
	.open           = logbuffer_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static int __init logbuffer_init(void)
{
	struct proc_dir_entry *entry;

	printk(KERN_INFO "logbuffer: driver init\n");

	entry = proc_create("logbuffer", 0, NULL, &logbuffer_operations);
	return 0;
}

static void __exit logbuffer_exit(void)
{
	printk(KERN_NOTICE "logbuffer: driver exit\n");
	remove_proc_entry("logbuffer", NULL);
}

module_param(buffer_info, charp, 0600);
MODULE_PARM_DESC(buffer_info,
	"Location information about the logbuffer. Format = name,length,address");

module_init(logbuffer_init);
module_exit(logbuffer_exit);
