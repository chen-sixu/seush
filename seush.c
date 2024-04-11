#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/wait.h>
#include<errno.h>
#define MAXLINE    1024
#define MAXARGS     128
#define MAXJOBS      16
#define MAXJID    1<<16
#define UNDEF 0

extern char **environ;      /* defined in libc */
char prompt[] = "seush> ";    /* command line prompt  */
int verbose = 0;
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLINE];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLINE];  /* command line */
};
struct job_t jobs[MAXJOBS]; 

void eval(char *cmdline);

int builtin_cmd(char **argv);

void addpath(char **argv);

void change_dir(char **argv);

int parseline(const char *cmdline, char **argv);

void clearjob(struct job_t *job);

void initjobs(struct job_t *jobs);

int maxjid(struct job_t *jobs); 

int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);

int deletejob(struct job_t *jobs, pid_t pid); 

struct job_t *getjobpid(struct job_t *jobs, pid_t pid);

struct job_t *getjobjid(struct job_t *jobs, int jid); 

void unix_error(char *msg);

void app_error(char *msg);

//封装
pid_t Fork(void)
{
    pid_t pid;
    if((pid=fork())<0)
    unix_error("Fork error");
    return pid;
}

void Execv(const char *path, char *const argv[])
{
    if(execv(path, argv) < 0)
    printf("%s: Command not found.\n", argv[0]);
}

void Setpgid(pid_t pid, pid_t pgid)
{
    if(setpgid(pid, pgid) < 0)
    unix_error("Setpid error");
}

int main(int argc,char **argv)
{
    char cmdline[MAXLINE];

    dup2(1,2);
    initjobs(jobs);
    while (1)
    {
	    if ((fgets(cmdline, MAXLINE, stdin) == NULL) && ferror(stdin))
	        app_error("fgets error");
	    if (feof(stdin))
        { 
	        fflush(stdout);
	        exit(0);
        }
        eval(cmdline);
	    fflush(stdout);
    }
    exit(0);
}

void eval(char *cmdline)
{
    char *argv[MAXARGS];
    char buf[MAXLINE];
    int state;
    pid_t pid;

    strcpy(buf,cmdline);
    state=parseline(cmdline,argv);
    if (argv[0]==NULL) 
        return;
    
    if (!builtin_cmd(argv))
    {
        if ((pid=Fork())==0)//创建子进程
        {
            Setpgid(0,0);
            Execv(path,argv[0]);
            exit(0);
        }
        addjob(jobs,pid,state,cmdline);
        waitpid(pid,NULL,NULL);
    }
    return;
}

//将cmdline解析为一系列参数argv
int parseline(const char *cmdline,char **argv)
{
    static char array[MAXLINE];
    char *buf = array;
    char *delim;
    int argc;
    
    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';
    while (*buf && (*buf == ' '))
	buf++;

    argc = 0;
    if (*buf == '\'') {
	buf++;
	delim = strchr(buf, '\'');
    }
    else {
	delim = strchr(buf, ' ');
    }

    while (delim) {
	argv[argc++] = buf;
	*delim = '\0';
	buf = delim + 1;
	while (*buf && (*buf == ' ')) 
	       buf++;

	if (*buf == '\'') {
	    buf++;
	    delim = strchr(buf, '\'');
	}
	else {
	    delim = strchr(buf, ' ');
	}
    }
    argv[argc] = NULL;
    
    if (argc == 0)
	return 1;

    //need bg/fg
}

int builtin_cmd(char **argv) 
{
    if(!strcmp(argv[0],"exit"))
    {
        exit(0);
    }
    if(!strcmp(argv[0],"cd"))
    {
        change_dir(argv);
        return 1;
    }
    if(!strcmp(argv[0],"path"))
    {
        addpath(argv);
        return 1;
    }
    return 0;     /* not a builtin command */
}

void change_dir(char **argv)
{

}

void addpath(char **argv)
{

}

void clearjob(struct job_t *job) 
{
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

void initjobs(struct job_t *jobs) 
{
    int i;
    for (i = 0; i < MAXJOBS; i++)
	clearjob(&jobs[i]);
}

int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
    int i;
    
    if (pid < 1)
	return 0;

    for (i = 0; i < MAXJOBS; i++) {
	if (jobs[i].pid == 0) {
	    jobs[i].pid = pid;
	    jobs[i].state = state;
	    jobs[i].jid = nextjid++;
	    if (nextjid > MAXJOBS)
		nextjid = 1;
	    strcpy(jobs[i].cmdline, cmdline);
  	    if(verbose){
	        printf("Added job [%d] %d %s\n", jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
            }
            return 1;
	}
    }
    printf("Tried to create too many jobs\n");
    return 0;
}



void unix_error(char *msg)
{
    fprintf(stdout, "%s: %s\n", msg, strerror(errno));
    exit(1);
}

void app_error(char *msg)
{
    fprintf(stdout, "%s\n", msg);
    exit(1);
}