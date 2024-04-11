#include<signal.h>
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
#define FG 1
#define BG 2
#define ST 3
#ifndef SA_RESTART
#define SA_RESTART 0x00000001
#endif

extern char **environ;
char prompt[] = "seush> ";
int verbose = 0;
int nextjid = 1;
char sbuf[MAXLINE];

struct job_t {              
    pid_t pid;              
    int jid;                
    int state;
    char cmdline[MAXLINE];
};
struct job_t jobs[MAXJOBS]; 

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);
void sigquit_handler(int sig);
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
typedef void handler_t(int);
handler_t *Signal(int signum, handler_t *handler);

//封装
pid_t Fork(void)
{
    pid_t pid;
    if((pid=fork())<0)
    unix_error("Fork error");
    return pid;
}

void Sigprocmask(int how,const sigset_t *set,sigset_t *oldset)
{
    if(sigprocmask(how,set,oldset)<0)
    unix_error("Sigprocmask error");
}

void Sigemptyset(sigset_t *set)
{
    if(sigemptyset(set)<0)
    unix_error("Sigemptyset error");
}

void Sigfillset(sigset_t *set)
{
    if(sigfillset(set)<0)
    unix_error("Sigfillset error");
}

void Sigaddset(sigset_t *set,int signum)
{
    if(sigaddset(set,signum)<0)
    unix_error("Sigaddset error");
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

void Kill(pid_t pid, int sig)
{
    if(kill(pid, sig) < 0)
    unix_error("Kill error");
}

int Sigsuspend(const sigset_t *mask)
{
    int temp=sigsuspend(mask);
    if (errno!=EINTR)
    {
        unix_error("Sigsuspend error");
    }
    return temp;
    
}

int main(int argc,char **argv)
{
    char cmdline[MAXLINE];

    dup2(1,2);

    Signal(SIGINT,  sigint_handler);  
    Signal(SIGTSTP, sigtstp_handler); 
    Signal(SIGCHLD, sigchld_handler); 
    Signal(SIGQUIT, sigquit_handler); 

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

/*解析命令行，处理（非）内置命令*/
void eval(char *cmdline)
{
    char *argv[MAXARGS];
    char buf[MAXLINE];     //保留修改后的命令行
    int bg;                //前台or后台
    int state;
    pid_t pid;

    strcpy(buf,cmdline);
    bg=parseline(buf,argv);
    state=bg?BG:FG;

    if(argv[0]==NULL)
        return;            //ignore empty lines
    

    sigset_t mask_all,mask_one,prev_one;
    Sigfillset(&mask_all);//所有信号添加到mask_all中
    Sigemptyset(&mask_one);//mask_one清空
    Sigaddset(&mask_one,SIGCHLD);//mask_one = SIGCHLD
    Signal(SIGCHLD,sigchld_handler);

    if(!builtin_cmd(argv))
    {//不是内置命令
        Sigprocmask(SIG_BLOCK,&mask_one,&prev_one);
        if ((pid=Fork())==0)//创建子进程
        {
            Sigprocmask(SIG_SETMASK,&prev_one,NULL);
            Setpgid(0,0);
            Execve(argv[0],argv,environ);
            exit(0);
        }
        if(state==FG)     //交互式
        {
            Sigprocmask(SIG_BLOCK,&mask_all,NULL);
            addjob(jobs,pid,state,cmdline);
            Sigprocmask(SIG_SETMASK,&mask_one,NULL);
            waitfg(pid);
        }
        else              //批处理
        {
            Sigprocmask(SIG_BLOCK,&mask_all,NULL);
            addjob(jobs,pid,state,cmdline);
            Sigprocmask(SIG_SETMASK,&mask_one,NULL);
            printf("[%d] (%d) %s",pid2jid(pid),pid,cmdline);
        }
        Sigprocmask(SIG_SETMASK,&prev_one,NULL);
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
    int bg;
    
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

    if ((bg = (*argv[argc-1] == '&')) != 0) //后台（批处理）模式的命令行以&结尾
    {
	argv[--argc] = NULL;
    }
    return bg;
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
    if(!strcmp(argv[0],"bg")||!strcmp(argv[0],"fg"))
    {
        do_bgfg(argv);
        return 1;
    }
    if(!strcmp(argv[0],"path"))
    {
        addpath(argv);
        return 1;
    }
    return 0;
}

void change_dir(char **argv)
{

}

void addpath(char **argv)
{

}

void do_bgfg(char **argv) 
{
    //判断传入的参数是jid还是pid，目的是为了找到对应的job
    //判断是bg还是fg
    struct job_t *job=NULL;
    int state;
    int id;

    if (!strcmp(argv[0],"bg"))
        state=BG;
    else
        state=FG;


    if (argv[1]==NULL)     //need argument
    {
        printf("%s command requires PID or %%jobid argument\n", argv[0]);
        return;
    }
    if (argv[1][0]=='%')  //jid
    {
        if (sscanf(&argv[1][1],"%d",&id)>0)
        {
            job=getjobjid(jobs,id);
            if (job==NULL)
            {
                printf("%%%d: No such job\n", id);
                return;
            }
        }
    }
    else if (!isdigit(argv[1][0]))  //not %,not a digit
    {
        printf("%s: argument must be a PID or %%jobid\n", argv[0]);
        return;
    }
    else                  //pid
    {
        id=atoi(argv[1]);
        job=getjobpid(jobs,id);
        if(job==NULL)
        {
            printf("(%d): No such process\n", id);
            return;
        }
    }

    kill(-(job->pid),SIGCONT);
    job->state=state;
    //表达是在bg还是fg
    if (state==BG)
    {
        printf("[%d] (%d) %s",job->jid,job->pid,job->cmdline);
    }
    else//fg
    {
        waitfg(job->pid);
    }
    return;
}

void waitfg(pid_t pid)
{
    sigset_t mask;
    Sigemptyset(&mask);
    while (fgpid(jobs)>0)
    {
        Sigsuspend(&mask);
    }    
    return;
}

void sigchld_handler(int sig) //回收僵尸进程
{//SIGSTOP SIGTSTP 正常终止
    int olderrno=errno;
    int status;
    sigset_t mask_all,prev_all;
    pid_t pid;
    struct job_t *job;

    Sigfillset(&mask_all);
    while ((pid=waitpid(-1,&status,WNOHANG|WUNTRACED))>0)//-1 指针对所有子进程；对status赋值；立即返回终止子进程的pid
    {
        Sigprocmask(SIG_BLOCK,&mask_all,&prev_all);//阻塞所有信号
        if (WIFEXITED(status))//正常终止
        deletejob(jobs,pid);
        else if (WIFSTOPPED(status))//SIGSTOP 因信号停止
        {
            printf ("Job [%d] (%d) stoped by signal %d\n", pid2jid(pid), pid, WSTOPSIG(status));
            job=getjobpid(jobs,pid);
            job->state=ST;
        }
        else if(WIFSIGNALED(status))//SIGTSTP 因信号终止
        {
            printf ("Job [%d] (%d) terminated by signal %d\n", pid2jid(pid), pid, WTERMSIG(status));
            deletejob(jobs,pid);
        }
        Sigprocmask(SIG_SETMASK,&prev_all,NULL);//打开
    }
    errno=olderrno;
    return;
}

void sigint_handler(int sig) //ctrl-c 终止前台工作
{
    int olderrno =errno;
    pid_t pid;
    sigset_t mask_all,prev_all;
    Sigfillset(&mask_all);
    Sigprocmask(SIG_BLOCK,&mask_all,&prev_all);
    if((pid=fgpid(jobs))!=0)
    {
        Sigprocmask(SIG_SETMASK,&prev_all,NULL);
        kill(-pid,SIGINT);
    }
    errno=olderrno;
    return;
}

void sigtstp_handler(int sig) //ctrl-z 挂起前台工作
{
    int olderrno =errno;
    pid_t pid;
    sigset_t mask_all,prev_all;
    Sigfillset(&mask_all);
    Sigprocmask(SIG_BLOCK,&mask_all,&prev_all);
    if((pid=fgpid(jobs))!=0)
    {
        Sigprocmask(SIG_SETMASK,&prev_all,NULL);
        kill(-pid,SIGTSTP);
    }
    errno=olderrno;
    return;
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

handler_t *Signal(int signum, handler_t *handler) 
{
    struct sigaction action, old_action;

    action.sa_handler = handler;  
    sigemptyset(&action.sa_mask); /* block sigs of type being handled */
    action.sa_flags = SA_RESTART; /* restart syscalls if possible */
    if (sigaction(signum, &action, &old_action) < 0)
	unix_error("Signal error");
    return (old_action.sa_handler);
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

void sigquit_handler(int sig) 
{
    printf("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}
