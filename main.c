#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wait.h>

#define MAX_LEN 1028
#define MAX_ARGS 127
#define TRUE 1
#define FALSE 0
#define PIPE "|"
#define BACKGROUND "&"
#define REDIR_IN "<"
#define REDIR_OUT ">"
#define REDIR_ERR "2>"
#define RUNNING 0
#define STOPPED 1
#define DONE 2

// struct to represent and manage in stack jobs
typedef struct Job {
  struct Job* prevJob;
  int jobNum;
  int status;       // 0=running, 1=stopped, 2=done/completed
  char* jobString;  // original command
  pid_t pgid;       // group id
  pid_t leftChildID;
  pid_t rightChildID;
  int isBackground;  // boolean. 1=yes, 2=no
  struct Job* nextJob;
} Job;

/**
 * @brief creates a new Job "object" like OOP language would. Callers need to
 * handle stack and jobNum
 *
 * @param pid1 first command
 * @param pid2 (if any) second command. (if none) put -1
 * @param isBackground boolean. 1=start from background, 0=otherwise
 * @param jobString the original command string input
 * @return Job* pointer to a malloc'd Job object
 */
Job* newJob(int pid1, int pid2, int isBackground, char* jobString) {
  Job* job = malloc(sizeof(Job));

  // fill up known inputs
  job->pgid = pid1;
  job->leftChildID = pid1;
  job->rightChildID = pid2;
  job->isBackground = isBackground;
  job->jobString = jobString;
  if (isBackground) {
    // null the space before '&' in jobString (assumed end with " &")
    int cmd_len = strlen(jobString);
    jobString[cmd_len - 2] = 0x00;
  }

  // job defaulted to running upon creation
  job->status = RUNNING;

  // no association with stack for now. caller handles stack interaction
  job->jobNum = -1;
  job->prevJob = NULL;
  job->nextJob = NULL;

  return job;
}
/**
 * @brief delete job obj similar to c++. frees the jobString (original cmd) too.
 * Caller responsible for severing job's connetion in the stack.
 *
 * @param job the job obj to be freed
 */
void delJob(Job* job) {
  free(job->jobString);
  free(job);
}

// nodes represting base and top of job stack
Job* stack_base = NULL;
Job* stack_top = NULL;

/**
 * @brief assess if the two string inputs are equal
 *
 * @param s1 first string input
 * @param s2 second string input
 * @return boolean. 1/true for same; 0/false for different
 */
int equal(const char* s1, const char* s2) {
  return (strcmp(s1, s2) == 0);
}

/**
 * @brief Appends job to doubly-linked job stack
 *
 * @param job the job to add onto program stack (on yash process)
 */
void appendJobToStack(Job* job) {
  if (stack_base == NULL) {
    // if stack empty, set job as top and bottom since its the only one
    stack_base = job;
    stack_top = job;
    job->jobNum = 1;  // the first job
  } else {
    // if stack has jobs, set job as top and connect with previous top
    Job* prevJob = stack_top;
    stack_top = job;

    prevJob->nextJob = job;
    job->prevJob = prevJob;

    job->jobNum = 1 + prevJob->jobNum;
  }
}
/**
 * @brief cut ties of input job from the stack and heals the stack
 *
 * @param currJob the Job to be removed from the stack
 */
void removeJobFromStack(Job* currJob) {
  Job* prev = currJob->prevJob;
  Job* next = currJob->nextJob;

  if (next) {
    // when there are more behind this job, pass currJob's prev back
    next->prevJob = currJob->prevJob;
  } else {
    // currJob is on the top of stack. set stack_top to prev (NULL or JOB*)
    stack_top = prev;
  }

  if (prev) {
    // when there are more preceed this job, pass currJob's next forward
    prev->nextJob = currJob->nextJob;
  } else {
    // currJob is on the base of stack. set stack_base to next (NULL or JOB*)
    stack_base = next;
  }
}
/**
 * @brief based on bg job stack, return the most recent running or stopped job
 *
 * @return Job* pointer to Job that can be brought to foreground via 'fg'
 */
Job* getNextJobInLine() {
  Job* ret = NULL;
  for (Job* curr = stack_top; curr; curr = curr->prevJob) {
    if (curr->status == RUNNING || curr->status == STOPPED) {
      return curr;
    }
  }
  return ret;
}

/**
 * @brief print a single Job's summary
 *
 * @param curr Job*, the job to be printed
 * @param fgCandidate Job*, if equal to curr then print '+', else print '-'
 */
void printJob(Job* curr, Job* fgCandidate) {
  char* status = (curr->status == RUNNING
                      ? "Running"
                      : (curr->status == STOPPED ? "Stopped" : "Done"));
  printf("[%d] %c %s\t%s%s\n", curr->jobNum, (curr == fgCandidate ? '+' : '-'),
         status, curr->jobString, (curr->status == RUNNING ? " &" : ""));
}

/**
 * @brief Prints the current stack of jobs in order, under uniform format
 */
void printJobs() {
  Job* fgCandidate = getNextJobInLine();
  for (Job* curr = stack_base; curr; curr = curr->nextJob) {
    printJob(curr, fgCandidate);
  }
}

/**
 * @brief Physically removes process that are done executing from the stack
 *
 * @param skipMessage boolean, true to not display bg done jobs
 */
void updateJobStack(int skipMessage) {
  // go thorough all processes on stack, update status of each one, remove done
  Job* currJob = stack_base;
  while (currJob) {
    // update stack
    if (currJob->status == DONE) {
      if (currJob->isBackground && !skipMessage) {
        printJob(currJob, NULL);
      }
      // remove this job from stack and free it
      removeJobFromStack(currJob);
      Job* dyingJob = currJob;     // mark currJob as dead
      currJob = currJob->nextJob;  // increment loop
      delJob(dyingJob);            // kill popped job
      continue;                    // no need to check dead job
    }

    // // update job status
    // int status;  // used for probing process status
    // int ret = waitpid(-1 * currJob->pgid, &status, WNOHANG | WUNTRACED);
    // if (ret != 0 && ret != -1) {
    //   if (WIFEXITED(status)) {  // if job exited normally
    //     currJob->status = DONE;
    //   } else if (WIFSTOPPED(status)) {  // if job stopped by signal
    //     currJob->status = STOPPED;
    //   } else if (WIFCONTINUED(status)) {  // if job resumed by SIGCONT
    //     currJob->status = RUNNING;
    //   }
    // }
    currJob = currJob->nextJob;  // increment loop
  }
}

void updateJobStatus() {
  Job* currJob = stack_base;
  while (currJob) {
    // update job status
    // int status;  // used for probing process status
    // int ret = waitpid(-1 * currJob->pgid, &status, WNOHANG | WUNTRACED);
    // if (ret != 0 && ret != -1) {
    //   if (WIFEXITED(status)) {  // if job exited normally
    //     currJob->status = DONE;
    //     printf("DONE! %s\n", currJob->jobString);
    //   } else if (WIFSTOPPED(status)) {  // if job stopped by signal
    //     currJob->status = STOPPED;
    //     printf("STOPPED! %s\n", currJob->jobString);
    //   } else if (WIFCONTINUED(status)) {  // if job resumed by SIGCONT
    //     currJob->status = RUNNING;
    //     printf("RUNNING! %s\n", currJob->jobString);
    //   }
    // }

    if (waitpid(-1 * currJob->pgid, NULL, WNOHANG | WUNTRACED) != 0 &&
        currJob->status == RUNNING) {
      currJob->status = DONE;
    }

    currJob = currJob->nextJob;  // increment loop
  }
}

/**
 * @brief Sends current stopped process to background
 */
void send_to_back() {
  // TODO
}

/**
 * @brief Brings process to foreground (wait)
 */
void bring_to_front() {
  printf("front");
}

/**
 * @brief Kills process or process groups that are associated with a pid
 *
 * @param pid PID of process group to remove
 */
void kill_proc(int pid) {
  kill(pid, SIGKILL);
}

/**
 * @brief Sets file redirections (stdin,stdout) across whole command string
 *
 * @param tokens parsed list of args
 * @param numToks overall number of args (not including &)
 */
void redirect(char* tokens[], int numToks) {
  int fd_in, fd_out, fd_err;
  // goes through each token to check for [<, >, 2>]
  for (int i = 0; i < numToks - 1; i++) {
    char* currToken = tokens[i];
    char* nextToken = tokens[i + 1];
    if (equal(currToken, REDIR_IN)) {  // "<" command
      tokens[i] = NULL;                // remove operator from tokens
      fd_in = open(nextToken, O_RDONLY);
      if (fd_in < 0) {
        perror(nextToken);
        _exit(1);
      }
      dup2(fd_in, STDIN_FILENO);
      close(fd_in);
      i++;  // next token is file, no need to check
    } else if (equal(currToken, REDIR_OUT)) {  // ">" command
      tokens[i] = NULL;                        // remove operator from tokens
      // char* outputFile = strdup(nextToken);
      fd_out = open(nextToken, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
      if (fd_out < 0) {
        perror(currToken);
        _exit(1);
      }
      dup2(fd_out, STDOUT_FILENO);
      close(fd_out);
      i++;  // next token is file, no need to check
    } else if (equal(currToken, REDIR_ERR)) {  // "2>" command
      tokens[i] = NULL;                        // remove operator from tokens
      fd_err = open(nextToken, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
      if (fd_err < 0) {
        perror(currToken);
        _exit(1);
      }
      dup2(fd_err, STDERR_FILENO);
      close(fd_err);
      i++;  // next token is file, no need to check
    }
  }
}

/**
 * @brief Executes input command using execvp
 *
 * @param cmdTokens parsed command list of strings
 * @param numToks number of items in cmdTokens
 * @param cmdInit original command string for printing purposes
 * @param bg background toggle for setting wait
 */
void executeCommand(char* cmdTokens[],
                    int numToks,
                    char* inputCmd,
                    int isBackground) {
  pid_t PID = fork();
  if (PID == 0) {
    // inside child process
    setpgid(0, 0);
    redirect(cmdTokens, numToks);
    execvp(cmdTokens[0], cmdTokens);
    fprintf(stderr, "BAD COMMAND\n");  // child not supposed to get here
    _exit(1);
  } else if (PID > 0) {
    // TODO: inside parent process

    Job* job = newJob(PID, -1, isBackground, inputCmd);  // job obj of this cmd

    if (!isBackground) {
      waitpid(PID, NULL, WUNTRACED);
    } else {
      appendJobToStack(job);
    }
    printf("returned to main process\n");
  } else {
    // fork failed
    printf("Fork failure, returned PID=%d\n", PID);
  }
}

/**
 * @brief Executes piped input command using execvp calls with this format:
 *        cmd1 | cmd2
 *
 * @param cmd1 parsed command lists of strings
 * @param cmd1_len numbers of tokens of cmd1
 * @param cmd2 original command
 * @param cmd2_len numbers of tokens of cmd2
 * @param initInput string for printing purposes
 * @param bg background toggle for setting wait
 */
void executeTwoCommands(char* cmd1[],
                        int cmd1_len,
                        char* cmd2[],
                        int cmd2_len,
                        char* inputCmd,
                        int isBackground) {
  int pfd[2];  // pipe between the two commands. cmd1=>pfd[1], pfd[0]=>cmd2
  pipe(pfd);
  pid_t p1 = fork();
  if (p1 > 0) {
    // TODO: parent process

  } else if (p1 == 0) {
    // left cmd
    setpgid(0, 0);  // create new process group led by left cmd
    dup2(pfd[1], STDOUT_FILENO);
    close(pfd[0]);
    redirect(cmd1, cmd1_len);
    execvp(cmd1[0], cmd1);
    fprintf(stderr, "BAD COMMAND on left side\n");
    _exit(1);
  }
  pid_t p2 = fork();
  if (p2 == 0) {
    // right cmd
    setpgid(0, p1);  // join process group led by left cmd
    dup2(pfd[0], STDIN_FILENO);
    close(pfd[1]);
    redirect(cmd2, cmd2_len);
    execvp(cmd2[0], cmd2);
    fprintf(stderr, "BAD COMMAND on right side\n");
    _exit(1);
  }
  close(pfd[0]);
  close(pfd[1]);
  if (p1 < 0 || p2 < 0) {
    printf("Fork failure, returned pid1=%d, pid2=%d\n", p1, p2);
    return;
  }
  Job* job = newJob(p1, p2, isBackground, inputCmd);  // job obj of this cmd
  if (!isBackground) {
    waitpid(-1, NULL, /*WNOHANG | */ WUNTRACED);  // wait for one of them to end
    waitpid(-1, NULL, /*WNOHANG | */ WUNTRACED);  // and the other one
  } else {
    appendJobToStack(job);
  }
  printf("returned to main process\n");
}

/**
 * @brief execute shell commands if present. OW return false
 *
 * @param tokens tokenized command input
 * @return boolean TRUE if the first token is one of ['fg', 'bg', 'jobs']
 */
int shellExecute(char* tokens[]) {
  if (!tokens || !tokens[0]) {
    printf("No commands\n");
    return FALSE;
  }
  if (equal(tokens[0], "fg")) {
    printf("fg command operation\n");
    return TRUE;
  }
  if (equal(tokens[0], "bg")) {
    printf("bg command operation\n");
    return TRUE;
  }
  if (equal(tokens[0], "jobs")) {
    printJobs();
    return TRUE;
  }
  return FALSE;
}

void tokenize(char* cmd, char* tokenList[], int* numToks, int* pipeIndex) {
  char* token;
  while ((token = strtok_r(cmd, " ", &cmd))) {
    tokenList[*numToks] = token;     // append token to command array
    if (equal(token, PIPE)) {        // check if command has a pipe
      *pipeIndex = *numToks;         // remembers the location of the pipe
      tokenList[*pipeIndex] = NULL;  // null terminates cmd1
    }
    (*numToks)++;
  }
  tokenList[*numToks] = NULL;  // null terminate args
}

/**
 * @brief Processes and validates input, splits command into pipe if necessary,
 * executes commands
 *
 * @param initCmd original command string
 */
void process(char* inputCmd) {
  // a copy of input command to mess around with
  char* cmdCopy = strdup(inputCmd);
  char* args[MAX_ARGS];
  int pipeIndex = -1;
  int isBackground = FALSE;  // 1/TRUE if cmd ends with '&'

  int numArgs = 0;
  // parses input command string to get args
  tokenize(cmdCopy, args, &numArgs, &pipeIndex);
  if (numArgs == 0)
    return;  // skip this command if its empty

  if (shellExecute(args))
    return;  // if shell commands finished, skip everything else

  char* lastToken = args[numArgs - 1];
  if (equal(lastToken, BACKGROUND)) {
    printf("command started from background\n");
    isBackground = TRUE;
    args[numArgs - 1] = NULL;  // nullify '&' to not mess up command
    numArgs--;  // since '&' is gone, total numArgs needs to reflect that
    // TODO: start job from background
  }

  if (pipeIndex > 0) {
    // execute piped commmands
    char** cmd1 = args;  // cmd1 starts the same as input but ends at pipeIndex
    char** cmd2 = &args[pipeIndex + 1];  // cmd2 starts after pipeIndex
    executeTwoCommands(cmd1, pipeIndex, cmd2, numArgs - pipeIndex - 1, inputCmd,
                       isBackground);
  } else if (pipeIndex < 0) {
    // execute regular command
    executeCommand(args, numArgs, inputCmd, isBackground);
  } else {
    fprintf(stderr, "syntax error near unexpected token '|'\n");
  }
}

/**
 * @brief Handles interrupt command
 */
void sig_int() {
  printf("\npressed ctrl+c, interrupt\n");
}

/**
 * @brief Handles halt command
 */
void sig_tstp() {
  printf("\npressed ctrl+z, interactive stop\n");
}

int main() {
  // set signals to custom handlers
  signal(SIGTTOU, SIG_IGN);
  signal(SIGINT, sig_int);
  signal(SIGTSTP, sig_tstp);

  // give terminal control to yash by default
  tcsetpgrp(0, getpid());

  while (TRUE) {
    char* cmd = readline("# ");
    if (cmd == NULL)
      _exit(0);
    if (strlen(cmd) <= 0)
      continue;
    process(cmd);
    updateJobStack(equal(cmd, "jobs"));
    updateJobStatus();
  }
}