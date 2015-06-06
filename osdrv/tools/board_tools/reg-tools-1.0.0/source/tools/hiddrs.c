#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <sys/mman.h>

#include "memmap.h"
#include "hi.h"
#include "strfunc.h"

#ifdef __cplusplus
#if __cplusplus
extern "C"{
#endif
#endif /* __cplusplus */

extern int errno;

#define OPEN_FILE "/dev/mem"
#define DDR_STAT_SIG SIGUSR1

#define PAGE_SIZE_MASK 0xfffff000

#define TIMER_INTERVAL 1

#define DDRC_BASE_ADDR 0x20110000
#define DDRC_MAP_LENGTH 0x20000

#define DDRC0_ADDR 0x00000
#define DDRC1_ADDR 0x10000

#define DDRC_TEST7 0x260
#define DDRC_TEST8 0x264
#define DDRC_TEST9 0x268

#define DDRC_VALUE 0xd47868c0
#define ONE_SECOND 0x47868c0
#define MAX_SECOND 3

#define reg_read(addr) (*(volatile unsigned int *)(addr))
#define reg_write(addr, val) (*(volatile unsigned int *)(addr) = (val))

static int fd;

static unsigned char *ddrc_base_addr;
static unsigned char *ddrc0_addr;
static unsigned char *ddrc1_addr;

static struct sigaction sa;
static timer_t tt;
static unsigned int timer_interval = TIMER_INTERVAL;
static unsigned int reg_value;
static unsigned int ddrc_num;

#define IO_ADDRESS(addr, off) ((unsigned char *)(addr) + off)

static int ddrc_remap(unsigned int ddrc_num);
static void ddrc_unmap(unsigned int ddrc_num);
static void print_usage(void);
static int parse_args(int argc, char *argv[],
		unsigned int *second, unsigned int *ddrc);

#define read_ddrc_reg(addr, tmp1, tmp2) do {\
	do {\
		tmp1 = reg_read(IO_ADDRESS(addr, DDRC_TEST7));\
	} while (tmp1 & (0x1 << 30));\
	ddr_read = reg_read(IO_ADDRESS(addr, DDRC_TEST8));\
	ddr_write = reg_read(IO_ADDRESS(addr, DDRC_TEST9));\
	tmp1 = reg_value & 0xfffffff;\
	tmp1 *= 2;\
	tmp2 = ddr_read + ddr_write;\
} while (0)

static void ddr_statistic(int n)
{
	unsigned int ddr_read = 0;
	unsigned int ddr_write = 0;
	unsigned int tmp1 = 0;
	double tmp2 = 0.0;
	float ddrc0_rate = 0.0f;
	float ddrc1_rate = 0.0f;

	if (0 == ddrc_num) {
		read_ddrc_reg(ddrc0_addr, tmp1, tmp2);
		ddrc0_rate = (tmp2 / tmp1) * 100;
		printf("ddrc0[%0.2f%%]\n", ddrc0_rate);
		reg_write(IO_ADDRESS(ddrc0_addr, DDRC_TEST7), reg_value);
	} else if (1 == ddrc_num) {
		read_ddrc_reg(ddrc1_addr, tmp1, tmp2);
		ddrc0_rate = (tmp2 / tmp1) * 100;
		printf("ddrc1[%0.2f%%]\n", ddrc0_rate);
		reg_write(IO_ADDRESS(ddrc1_addr, DDRC_TEST7), reg_value);
	} else if (2 == ddrc_num) {
		read_ddrc_reg(ddrc0_addr, tmp1, tmp2);
		ddrc0_rate = (tmp2 / tmp1) * 100;
		read_ddrc_reg(ddrc1_addr, tmp1, tmp2);
		ddrc1_rate = (tmp2 / tmp1) * 100;
		printf("ddrc0[%0.2f%%] ddrc1[%0.2f%%]\n", ddrc0_rate, ddrc1_rate);
		reg_write(IO_ADDRESS(ddrc0_addr, DDRC_TEST7), reg_value);
		reg_write(IO_ADDRESS(ddrc1_addr, DDRC_TEST7), reg_value);

	} else
		;

	return;
}

static void ddr_ctrl_c(int n)
{
	timer_delete(tt);
	ddrc_unmap(ddrc_num);
	close(fd);
	_exit(0);
}

static int ddrc_remap(unsigned int ddrc_num)
{
	unsigned int phy_addr_in_page = 0;

	phy_addr_in_page = DDRC_BASE_ADDR & PAGE_SIZE_MASK;

	ddrc_base_addr = (unsigned char *)mmap(NULL, DDRC_MAP_LENGTH,
			PROT_READ | PROT_WRITE, MAP_SHARED,
			fd, phy_addr_in_page);

	if (ddrc_base_addr == MAP_FAILED) {
		printf("ddr%d statistic mmap failed.\n", ddrc_num);
		return -1;
	}

	ddrc0_addr = ddrc_base_addr + DDRC0_ADDR;
	ddrc1_addr = ddrc_base_addr + DDRC1_ADDR;

	return 0;
}

static void ddrc_unmap(unsigned int ddrc_num)
{
	munmap(ddrc_base_addr, DDRC_MAP_LENGTH);
	return;
}

static void print_usage(void)
{
	printf("NAME\n");
	printf("  ddrs - ddr statistic\n\n");
	printf("DESCRIPTION\n");
	printf("  Statistic percentage of occupation of ddr.\n\n");
	printf("  -d, --ddrc\n");
	printf("      which ddrc you want statistic. \"0\" statistic ddrc0,\n");
	printf("      \"1\" statistic ddrc1, \"2\" statistic ddrc0 and ddrc1 at the same time. \"0\" as default.\n");
	printf("  -i, --interval\n");
	printf("      the range is 1~3 second, 1 second as default.\n");
	printf("  -h, --help\n");
	printf("      display this help and exit\n");
	printf("  eg:\n");
	printf("      $ ddrs -d 0 -i 1\n");
	return;
}

static int parse_args(int argc, char *argv[],
		unsigned int *second, unsigned int *ddrc)
{
	int i = 0, k = 0;
	int flags = 0;
	int _ddrc = 0;
	int _second = 0;

	if ((argc % 2) != 0)
		goto ERROR;

	for (i = 0; i < argc; i += 2) {
		flags = 0;
		if (0 == strcmp("-d", argv[i]) ||
				0 == strcmp("--ddrc",  argv[i])) {
			for (k = 0; k < strlen(argv[i + 1]); ++k) {
				if(0 == isdigit(argv[i + 1][k])) {
					flags = 1;
					break;
				}
			}
			if (!flags) {
				_ddrc = atoi(argv[i + 1]);
				if (_ddrc < 0 || _ddrc > 2)
					goto ERROR;
				*ddrc = _ddrc;
			} else
				goto ERROR;
		} else if (0 == strcmp("-i",  argv[i]) ||
				0 == strcmp("--interval", argv[i])) {
			for (k = 0; k < strlen(argv[i + 1]); ++k) {
				if (0 == isdigit(argv[i + 1][k])) {
					flags = 1;
					break;
				}
			}
			if (!flags) {
				_second = atoi(argv[i + 1]);
				if (_second < 0 || _second > 3)
					goto ERROR;
				*second = _second;
			} else
				goto ERROR;
		} else if (0 == strcmp("-h",  argv[i]) ||
				0 == strcmp("--help", argv[i])) {
			print_usage();
		} else
			goto ERROR;
	}

	return 0;
ERROR:
	print_usage();
	return -1;
}

//int main(int argc, char *argv[])
HI_RET hiddrs(int argc , char* argv[])
{
	int ret = 0;
	struct itimerspec tspec;
	struct sigevent timer_event;
	unsigned int second = TIMER_INTERVAL;
	/*statistic ddrc0 and ddrc1 at the same time */
	ddrc_num = 0;

	ret = parse_args(argc - 1 , &argv[1], &second, &ddrc_num);
	if (ret)
		return -1;

	timer_interval = second;

	fd = open(OPEN_FILE, O_RDWR | O_SYNC);
	if (-1 == fd) {
		perror("ddr statistic open failed:");
		goto ERR0;
	}
	
	ret = ddrc_remap(ddrc_num);
	if (ret)
		goto ERR1;

	sa.sa_flags = SA_RESTART;
	sa.sa_handler = ddr_statistic;

	sigemptyset(&sa.sa_mask);

	if (sigaction(DDR_STAT_SIG, &sa, NULL)) {
		printf("ddr statistic install signal failed.\n");
		goto ERR1;
	}

	timer_event.sigev_notify = SIGEV_SIGNAL;
	timer_event.sigev_signo = DDR_STAT_SIG;
	timer_event.sigev_value.sival_ptr = (void *)&tt;

	if (timer_create(CLOCK_REALTIME, &timer_event, &tt) < 0) {
		printf("ddr statistic timer create failed.\n");
		goto ERR2;
	}

	tspec.it_value.tv_sec = timer_interval;
	tspec.it_value.tv_nsec = 0;
	tspec.it_interval.tv_sec = timer_interval;
	tspec.it_interval.tv_nsec = 0;

	reg_value = timer_interval * ONE_SECOND;
	reg_value &= 0xfffffff;
	reg_value |= 0xd0000000;

	if (0 == ddrc_num)
		reg_write(IO_ADDRESS(ddrc0_addr, DDRC_TEST7), reg_value);
	else if (1 == ddrc_num)
		reg_write(IO_ADDRESS(ddrc1_addr, DDRC_TEST7), reg_value);
	else if (2 == ddrc_num) {
		reg_write(IO_ADDRESS(ddrc0_addr, DDRC_TEST7), reg_value);
		reg_write(IO_ADDRESS(ddrc1_addr, DDRC_TEST7), reg_value);
	} else
		;

	if (timer_settime(tt, TIMER_ABSTIME, &tspec, NULL) < 0) {
		printf("ddr statistic set timer failed.\n");
		goto ERR3;
	}

	printf("===== ddr statistic =====\n");

	struct sigaction sa_ctrl_c;
	sa_ctrl_c.sa_flags = SA_RESTART;
	sa_ctrl_c.sa_handler = ddr_ctrl_c;
	sigemptyset(&sa_ctrl_c.sa_mask);

	if (sigaction(SIGINT, &sa_ctrl_c, NULL)) {
		printf("ddr statistic catch SIGINT signal failed.\n");
		goto ERR3;
	}

	while (1)
		sleep(1);
	return 0;

ERR3:
	timer_delete(tt);
ERR2:
	ddrc_unmap(ddrc_num);
ERR1:
	close(fd);
ERR0:
	return -1;
}
#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */
