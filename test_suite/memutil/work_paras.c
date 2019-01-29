#include <stdio.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <libgen.h>

#include "mem_util.h"
#include "work_paras.h"



static struct working_params gt_working_params;

struct working_params *get_working_params(void)
{
	return &gt_working_params;
}

int verbose_enabled(void)
{
	struct working_params *work_para = get_working_params();
	return work_para->verbose;
}

#define    HELP                 (900)
#define    VERBOSE              (1001)
#define    START_PHY_ADDR       (1011)
#define    LENGTH               (1012)
#define    SAVE_TO_FILE 	(1013)
#define    LOAD_FROM_FILE 	(1014)
#define    FILE_PATH            (1015)

static struct option cmd_line_options[] =
{
	{"help",               no_argument,        NULL,  HELP},
	{"verbose",            no_argument,        NULL,  VERBOSE},
	{"start-phy-addr",     required_argument,  NULL,  START_PHY_ADDR},
	{"length",             required_argument,  NULL,  LENGTH},
	{"save-to-file",       no_argument,        NULL,  SAVE_TO_FILE},
	{"load-from-file",     no_argument,        NULL,  LOAD_FROM_FILE},
	{"file-path",          required_argument,  NULL,  FILE_PATH},
	{NULL},
};


static void print_usage(const char *app_name)
{
    NORMAL_PRINT("usage:\n"
    	   "    %s [start-phy-addr=addr] --length=n <--save-to-file | --load-from-file> --file-path=path\n\n"
           ,app_name);

    NORMAL_PRINT("options:\n\n"
           "    --help/-h/-H            : print this help message\n"
           "    --verbose               : enable detailed information output\n"
           "    --start-phy-addr=addr   : start physical address of memory region. default is 0\n"
           "    --length=n              : length of of memory region(in bytes)\n"
           "    --save-to-file          : save memory region to file\n"
           "    --load-from-file        : load file to memory region\n"
           "    --file-path=path        : file for save to or load from\n"
           "\n\n");

}

static int check_paras(struct working_params *work_para)
{
	if (!work_para->length) {
		ERR_PRINT("length of memory region must be specified.\n");
		goto ERR_EXIT;
	}

	if ((!work_para->save_to_file) && (!work_para->load_from_file)) {
		ERR_PRINT("action must be specified: --save-to-file or --load-from-file.\n");
		goto ERR_EXIT;
	}

	if ((work_para->save_to_file) && (work_para->load_from_file)) {
		ERR_PRINT("only one action can be specified: --save-to-file or --load-from-file.\n");
		goto ERR_EXIT;
	}

	if (!work_para->file_path) {
		ERR_PRINT("file path for save to or load from must be specified.\n");
		goto ERR_EXIT;
	}

	return 0;

ERR_EXIT:
	return -1;
}

static int has_verbose_opt(int argc, char *argv[])
{
	int i;

	for (i=1; i<argc; i++) {
		if (strcmp(argv[i], "--verbose")==0) {
			return 1;
		}
	}

	return 0;
}

int parse_cmdline_options(int argc, char *argv[])
{
	int opt, ret = 0;
	const char *app_name = basename(argv[0]);
	struct working_params *work_para = get_working_params();

	/* scan verbose option first to enable verbose print as soon as possible */
	work_para->verbose = has_verbose_opt(argc, argv);

	while ((opt = getopt_long(argc, argv, "hH", cmd_line_options, NULL)) != -1)
	{
		switch (opt) {
		case  VERBOSE:
			work_para->verbose = 1;
			VERBOSE_PRINT("verbose print enabled\n");
			break;

		case  START_PHY_ADDR:
			work_para->start_phy_addr = strtoull(optarg, NULL, 0);
			VERBOSE_PRINT("mem region start_phy_addr = 0x%llx(%llu)\n"
				, work_para->start_phy_addr
				, work_para->start_phy_addr);
			break;

		case  LENGTH:
			work_para->length = strtoull(optarg, NULL, 0);
			VERBOSE_PRINT("mem region length = 0x%llx(%llu)\n"
				, work_para->length
				, work_para->length);
			break;

		case  SAVE_TO_FILE:
			work_para->save_to_file = 1;
			VERBOSE_PRINT("specified action: save to file\n");
			break;

		case  LOAD_FROM_FILE:
			work_para->load_from_file = 1;
			VERBOSE_PRINT("specified action: load from file\n");
			break;

		case  FILE_PATH:
			work_para->file_path = optarg;
			VERBOSE_PRINT("file path = %s\n", work_para->file_path);
			break;


		case  'h':
		case  'H':
		case  HELP:
		default: /* '?' */
			ret = -1;
			print_usage(app_name);
			goto EXIT;
		}
	}

	ret = check_paras(work_para);
	if (ret)
		print_usage(app_name);
EXIT:
	return ret;
}

