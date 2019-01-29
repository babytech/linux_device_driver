/*
 * a common tool which can save specified memory region to specified file
 * or do the reverse job.
 */

#include "mem_util.h"
#include "work_paras.h"
#include "mem_ops.h"

int main(int argc, char *argv[])
{
	int ret = 0;
	struct working_params * work_para;

	if (parse_cmdline_options(argc, argv)) {
		return -1;
	}

	work_para = get_working_params();
	if (work_para->save_to_file)
		ret = save_mem_region_to_file(work_para);
	else if (work_para->load_from_file)
		ret = load_mem_region_from_file(work_para);

	return ret;
}

