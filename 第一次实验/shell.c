#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <pwd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <curses.h>
#include <limits.h>
#include <termcap.h>
#include <termios.h>
#include <time.h>
#include <dirent.h>
#include <assert.h>
#include <sys/ioctl.h>
#include <sys/times.h>
#include <sys/time.h>
#include <sys/select.h>
#include <minix/com.h>
#include <minix/config.h>
#include <minix/type.h>
#include <minix/endpoint.h>
#include <minix/const.h>
#include <minix/u64.h>
#include <paths.h>
#include <minix/procfs.h>
#define ORDER_CPU 0
#define ORDER_MEMORY 1
int order = ORDER_CPU;
/* name of cpu cycle types */
const char *cputimenames[] = {"user", "ipc", "kernelcall"};
#define CPUTIMENAMES (sizeof(cputimenames) / sizeof(cputimenames[0]))
#define CPUTIME(m, i) (m & (1L << (i)))
unsigned int nr_procs, nr_tasks;
int nr_total;
#define SLOT_NR(e) (_ENDPOINT_P(e) + nr_tasks)
#define USED 0x1
#define IS_TASK 0x2
#define IS_SYSTEM 0x4
#define MAXLINE 4096
#define MAXARG 20 //参数最多个数
#define SELF 0x8ace
#define _MAX_MAGIC_PROC (SELF)
#define _ENDPOINT_GENERATION_SIZE (MAX_NR_TASKS + _MAX_MAGIC_PROC + 1)
#define _ENDPOINT_P(e) ((((e) + MAX_NR_TASKS) % _ENDPOINT_GENERATION_SIZE) - MAX_NR_TASKS)
#define _PATH_PROC "/proc/"
#define STRUCT_EVAL_FUNCTION
#define BG 1   //后台执行标志位
#define IN_RED 2  //输入重定向标志位
#define OUT_RED 4 //输出重定向标志位
#define IS_PIPED 8	   //管道标志位
struct proc{
	int p_flags;
	endpoint_t p_endpoint; //端点
	pid_t p_pid;
	u64_t p_cpucycles[CPUTIMENAMES];
	int p_priority;
	endpoint_t p_blocked;
	time_t p_user_time;
};
struct proc *proc = NULL, *prev_proc = NULL;
struct tp
{
	struct proc *p;
	u64_t ticks;
};
struct eval_function;
char *buf;
int parseline(char **, char **, int *);			//读取命令函数
int builtin_cmd(char *, char **); 				//内建命令函数
int eval(char **, int, struct eval_function *);	//语法分析函数
struct eval_function
{
	int flag;			//表明使用了哪些功能的标志位
	char *in_file;		//输入重定向的文件名
	char *out_file;		//输出重定向的文件名
	char *cmd1;			//命令1
	char **pmt1; //命令1的参数表
	char *cmd2;			//命令2
	char **pmt2; //命令2的参数表
};
int HistoryIndex;
char **cmdHistory;
void parse_file(pid_t pid)
{
	char path[PATH_MAX], name[256], type, state;
	int version, endpt;
	unsigned long cycles_hi, cycles_lo;
	FILE *fp;
	struct proc *p;
	int slot;
	int i;
	sprintf(path, "%d/psinfo", pid);
	if ((fp = fopen(path, "r")) == NULL)
		return;
	if (fscanf(fp, "%d", &version) != 1)
	{
		fclose(fp);
		return;
	}
	if (fscanf(fp, " %c %d", &type, &endpt) != 2)
	{
		fclose(fp);
		return;
	}
	slot = SLOT_NR(endpt);
	if (slot < 0 || slot >= nr_total)
	{
		fclose(fp);
		return;
	}
	p = &proc[slot];
	if (type == TYPE_TASK)
		p->p_flags |= IS_TASK;
	else if (type == TYPE_SYSTEM)
		p->p_flags |= IS_SYSTEM;
	p->p_endpoint = endpt;
	p->p_pid = pid;
	if (fscanf(fp, " %255s %c %d %d %lu %*u %lu %lu", name, &state, &p->p_blocked, &p->p_priority, &p->p_user_time, &cycles_hi, &cycles_lo) != 7)
	{
		fclose(fp);
		return;
	}
	p->p_cpucycles[0] = make64(cycles_lo, cycles_hi);
	for (i = 1; i < CPUTIMENAMES; i++)
	{
		if (fscanf(fp, " %lu %lu", &cycles_hi, &cycles_lo) == 2)
		{
			p->p_cpucycles[i] = make64(cycles_lo, cycles_hi);
		}
		else
		{
			p->p_cpucycles[i] = 0;
		}
	}
	p->p_flags |= USED;
	fclose(fp);
}
void parse_dir(void)
{
	DIR *p_dir;
	struct dirent *p_ent;
	pid_t pid;
	char *end;
	if ((p_dir = opendir(".")) == NULL)
	{
		perror("opendir on ");
		exit(1);
	}
	for (p_ent = readdir(p_dir); p_ent != NULL; p_ent = readdir(p_dir))
	{
		pid = strtol(p_ent->d_name, &end, 10);
		if (!end[0] && pid != 0)
			parse_file(pid);
	}
	closedir(p_dir);
}
void get_procs(void)
{
	struct proc *p;
	int i;
	p = prev_proc;
	prev_proc = proc;
	proc = p;
	if (proc == NULL)
	{
		proc = malloc(nr_total * sizeof(proc[0]));
		if (proc == NULL)
		{
			fprintf(stderr, "Out of memory!\n");
			exit(1);
		}
	}
	for (i = 0; i < nr_total; i++)
		proc[i].p_flags = 0;
	parse_dir();
}
int print_memory(void)
{
	FILE *fp;
	unsigned int pagesize;
	unsigned long total, free, largest, cached;
	if ((fp = fopen("meminfo", "r")) == NULL)
		return 0;
	if (fscanf(fp, "%u %lu %lu %lu %lu", &pagesize, &total, &free, &largest, &cached) != 5)
	{
		fclose(fp);
		return 0;
	}
	fclose(fp);
	printf("main memory: %ldK total, %ldK free,%ldK cached\n", (pagesize * total) / 1024, (pagesize * free) / 1024, (pagesize * cached) / 1024);
	return 1;
}
void getkinfo(void)
{
	FILE *fp;
	if ((fp = fopen("kinfo", "r")) == NULL)
	{
		fprintf(stderr, "opening " _PATH_PROC "kinfo failed\n");
		exit(1);
	}
	if (fscanf(fp, "%u %u", &nr_procs, &nr_tasks) != 2)
	{
		fprintf(stderr, "reading from " _PATH_PROC "kinfo failed\n");
		exit(1);
	}
	fclose(fp);
	nr_total = (int)(nr_procs + nr_tasks);
}
u64_t cputicks(struct proc *p1, struct proc *p2, int timemode)
{
	int i;
	u64_t t = 0;
	for (i = 0; i < CPUTIMENAMES; i++)
	{
		if (!CPUTIME(timemode, i))
			continue;
		if (p1->p_endpoint == p2->p_endpoint)
		{
			t = t + p2->p_cpucycles[i] - p1->p_cpucycles[i];
		}
		else
		{
			t = t + p2->p_cpucycles[i];
		}
	}
	return t;
}
void print_procs(int maxlines, struct proc *proc1, struct proc *proc2, int cputimemode)
{
	int p, nprocs;
	u64_t idleticks = 0;
	u64_t kernelticks = 0;
	u64_t systemticks = 0;
	u64_t userticks = 0;
	u64_t total_ticks = 0;
	static struct tp *tick_procs = NULL;
	if (tick_procs == NULL)
	{
		tick_procs = malloc(nr_total * sizeof(tick_procs[0]));

		if (tick_procs == NULL)
		{
			fprintf(stderr, "Out of memory!\n");
			exit(1);
		}
	}
	for (p = nprocs = 0; p < nr_total; p++)
	{
		u64_t uticks;
		if (!(proc2[p].p_flags & USED))
			continue;
		tick_procs[nprocs].p = proc2 + p;
		tick_procs[nprocs].ticks = cputicks(&proc1[p], &proc2[p], cputimemode);
		uticks = cputicks(&proc1[p], &proc2[p], 1);
		total_ticks = total_ticks + uticks;
		if (p - NR_TASKS == IDLE)
		{
			idleticks = uticks;
			continue;
		}
		if (p - NR_TASKS == KERNEL)
		{
			kernelticks = uticks;
		}
		if (!(proc2[p].p_flags & IS_TASK))
		{
			if (proc2[p].p_flags & IS_SYSTEM)
				systemticks = systemticks + tick_procs[nprocs].ticks;
			else
				userticks = userticks + tick_procs[nprocs].ticks;
		}
		nprocs++;
	}
	printf("CPU states: %6.2f%% user, ", 100.0 * userticks / total_ticks);
	printf("%6.2f%% system, ", 100.0 * systemticks / total_ticks);
	printf("%6.2f%% kernel, ", 100.0 * kernelticks / total_ticks);
	printf("%6.2f%% idle\n", 100.0 * idleticks / total_ticks);
}
void showtop(int cputimemode, int r)
{
	int lines = 0;
	get_procs();
	if (prev_proc == NULL)
		get_procs();
	lines += print_memory();
	print_procs(r - lines - 2, prev_proc, proc, cputimemode);
	fflush(NULL);
}
int mytop(void)
{
	int r;
	int cputimemode = 1;

	if (chdir(_PATH_PROC) != 0)
	{
		perror("chdir to " _PATH_PROC);
		return 1;
	}
	getkinfo();
	showtop(cputimemode, r);
	chdir("/root/");
	return 1;
}
int main(int argc, char *argv[])
{
	int status, i;
	char *cmd = NULL;  
	char **pmt; 		
	int paranum;	  
	struct eval_function func;
	pid_t pid1, pid2;
	pmt = malloc(sizeof(char *) * (MAXARG + 2));
	buf = malloc(sizeof(char) * MAXLINE);
	//进入shell主体
	cmdHistory = (char **)malloc(sizeof(MAXLINE));
	HistoryIndex = 0;
	while (1){
		int fd[2], in_fd, out_fd;
		printf("&>");									//终端提示符
		paranum = parseline(&cmd, pmt, &HistoryIndex); //读入命令
		if (-1 == paranum)
			continue;
		paranum--;						  //命令数减一
		eval(pmt, paranum, &func); //对于后台执行、管道、重定向的初始化操作
		if (strcmp(cmd, "exit") == 0)
			exit(0);									   //实现exit
		if (builtin_cmd(cmd, pmt) == 1) //内建命令
			continue;
		if (func.flag & IS_PIPED) {//管道
			if (pipe(fd) < 0) {
				exit(0);
			}
		}
		if ((pid1 = fork()) != 0){ //shell主进程  父进程
			if (func.flag & IS_PIPED) {//是管道
				if ((pid2 = fork()) == 0) {//要求管道进程必须为shell进程的子进程
					close(fd[0]);
					close(1);
					dup(fd[1]);
					close(fd[1]);
					if (execvp(func.cmd1, func.pmt1) == -1)
						printf("%s\n", strerror(errno));
				}
				else {//管道进程的父进程 读写都没用
					close(fd[0]);
					close(fd[1]);
					waitpid(pid2, &status, 0); //wait cmd2
				}
			}
				waitpid(pid1, &status, 0); //等待命令1结束
		}
		else {//shell的子进程
			if (func.flag & IS_PIPED){ //命令2不为空
				close(fd[1]);
				close(0);
				dup(fd[0]);
				close(fd[0]);
				if (execvp(func.cmd2, func.pmt2) == -1)
					printf("%s\n", strerror(errno));
			}else {//命令2为空
				if (func.flag & OUT_RED) {// 输出重定向
					out_fd = open(func.out_file,O_RDWR|O_CREAT);
					close(1);
					dup(out_fd);
					close(out_fd);
				}
				if (func.flag & IN_RED) {//输入重定向
					in_fd = open(func.in_file,O_RDWR|O_CREAT); 
					close(0);
					dup(in_fd);
					close(in_fd);
				}
				if (func.flag & BG) {//后台执行
					int fd = open("/dev/null", O_RDWR);
					dup2(fd, 0);
					dup2(fd, 1);
					close(fd);
					signal(SIGCHLD, SIG_IGN);
				}
				if (execvp(cmd, pmt) == -1)
					printf("%s\n", strerror(errno));
			}
		}
	}
	free(pmt); 
	free(buf);	 
	return 0;
}
//读入命令，从用户输入中读取命令和参数，分别放入cmd[]和pmt[][]中
int parseline(char **cmd, char **pmt, int *HistoryIndex)
{
	if (fgets(buf, MAXLINE, stdin) == NULL) {//从键盘读入字符存入buf
		printf("\n");
		exit(0);
	}
	char *temp;
	if (*HistoryIndex > MAXLINE)
		*HistoryIndex = 0;
	temp = (char *)malloc(sizeof(buf));
	strcpy(temp, buf);
	cmdHistory[(*HistoryIndex)++] = temp;
	if (buf[0] == '\0')
		return -1;
	char *start, *end;
	int count = 0;
	int finishflag = 0;
	start = end = buf;
	while (finishflag == 0){
		while ((*end == ' ' && *start == ' ') || (*end == '\t' && *start == '\t')) {//忽略空格和tab字符
			start++;
			end++;
		}
		if (*end == '\0' || *end == '\n') {//到字符结尾
			break;
		}
		while (*end != ' ' && *end != '\0' && *end != '\n')
			end++;
		if (count == 0){
			char *p = end;
			*cmd = start; //命令存入cmd
			while (p != start && *p != '/')
				p--;
			pmt[0] = p; //参数存入pmt
			count += 2;
		}
		else if (count <= MAXARG){
			pmt[count - 1] = start;
			count++;
		}
		else{
			break;
		}
		if (*end == '\n') {//到输入字符末尾
			*end = '\0';
			finishflag = 1;
		}
		else{
			*end = '\0';
			end++;
			start = end;
		}
	}
	pmt[count - 1] = NULL;
	return count;
}
//语法分析函数，主要处理后台执行，重定向，管道功能实现
int eval(char **pmt, int paranum, struct eval_function *func)
{
	int i;
	func->flag = 0;			  //表明使用了哪些功能的标志位
	func->in_file = NULL;	  //输入重定向的文件名
	func->out_file = NULL;	  //输出重定向的文件名
	func->cmd2 = NULL;		  //命令2
	func->pmt2 = NULL; //命令2的参数表
	func->cmd1 = NULL;		  //命令1
	func->pmt1 = NULL; //命令1的参数表
	if (strcmp(pmt[paranum - 1], "&") == 0){ //后台执行
		func->flag |= BG;
		pmt[paranum - 1] = NULL;
		paranum--;
	}
	for (i = 0; i < paranum; i++){
		if (strcmp(pmt[i], "<") == 0){ //输入重定向
			func->flag |= IN_RED;
			func->in_file = pmt[i + 1]; //输入重定向的文件名
			pmt[i] = NULL;
			i += 2;
		}
		else if (strcmp(pmt[i], ">") == 0){ //输出重定向
			func->flag |= OUT_RED;
			func->out_file = pmt[i + 1]; //输出重定向的文件名
			pmt[i] = NULL;
			i += 2;
		}
		else if (strcmp(pmt[i], "|") == 0){ //管道
			func->flag |= IS_PIPED;
			pmt[i] = NULL;
			func->cmd1 = pmt[0];
			func->pmt1 = &pmt[0];
			func->cmd2 = pmt[i + 1];
			func->pmt2 = &pmt[i + 1];
		}
	}
	return 1;
}
/*print the cmd with n lines*/
void Printcmd(int n)
{
	int i,j=0;
	if (n == -1){
		for (i = 0; i < HistoryIndex; i++)
			printf("the %d cmd: %s", i, cmdHistory[i]);
	}
	else{
		if (n > HistoryIndex){
			printf("Warning: the argument is too large.\n");
			return;
		}
		for ( i = HistoryIndex - n; i < HistoryIndex; i++)
			printf("the %d cmd: %s", ++j, cmdHistory[i]);
	}
}
//内建命令函数，实现了cd、history、mytop命令
int builtin_cmd(char *cmd, char **pmt)
{
	if (strcmp(cmd, "cd") == 0) {//实现cd命令
		char *cd_path = NULL;
		cd_path = malloc(strlen(pmt[1] + 1)); 
		strcpy(cd_path, pmt[1]);
		chdir(cd_path);
		return 1;
		free(cd_path);
	}
	else if (strcmp(cmd, "history") == 0) {//实现history
		Printcmd(*pmt[1] - 48);
		return 1;
	}
	else if (strcmp(cmd, "mytop") == 0) {//实现mytop
		return mytop();
	}
	return 0;
}