#ifndef  __WORK_PARAS_H__
#define  __WORK_PARAS_H__

struct working_params{
	/* command line options */
	int			verbose;
	unsigned long long	start_phy_addr;
	unsigned long long	length;
	int			save_to_file;
	int			load_from_file;
	const char *		file_path;

	/* runtime para */
	void *			vaddr_base;
	unsigned long long      map_length;
	void *			vaddr;
};

int parse_cmdline_options(int argc, char *argv[]);
struct working_params *get_working_params(void);
int verbose_enabled(void);
#endif

