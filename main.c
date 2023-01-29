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
#define PIPE "|"
#define BACKGROUND "&"
#define REDIR_IN "<"
#define REDIR_OUT ">"
#define REDIR_ERR "2>"
#define JOBS "jobs"
#define BG "bg"
#define FG "fg"
#define EXIT "exit"
#define RUNNING "Running"
#define STOPPED "Stopped"
#define DONE "Done"
#define CUR_M "+"
#define BAK_M "-"
#define GEN_F "[%d]%s %s     %s\n"
#define DONE_F "[%d]%s %s        %s\n"

// struct to represent and manage in stack jobs
struct process {
  pid_t pid;   // id
  pid_t pgid;  // group id
  int status;  // 0=running; 1=stopped; 2=done
  int job_num;
  char* text;
  struct process* nextProcess;
  struct process* prevProcess;
  int isBackground;  // boolean
};

// nodes represting base and top of job stack
struct process* head = NULL;
struct process* top = NULL;

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
 * @brief Appends process to doubly-linked process stack
 *
 * @param args Original command string
 * @param pid1 first PID
 * @param pid2 second PID (set to -1 if no pipe in process cmd)
 * @param background background identifier
 */
void add_process(char* args, int pid1, int pid2, int background) {
  struct process* proc = malloc(sizeof(struct process));
  proc->text = args;
  proc->status = 0;
  proc->isBackground = background;

  // set process group as pid1 for both pids
  proc->pgid = pid1;
  setpgid(pid1, 0);
  if (pid2 == -1) {
    setpgid(pid2, pid1);
  }

  // if nothing on stack, add as head/top
  if (head == NULL) {
    proc->job_num = 1;
    proc->nextProcess = NULL;
    proc->prevProcess = NULL;
    head = proc;
    top = proc;
  }
  // if something on stack append to top
  else {
    proc->prevProcess = top;
    top->nextProcess = proc;
    proc->job_num = top->job_num + 1;
    top = proc;
  }
}

/**
 * @brief Physically removes process that are done executing from the stack
 */
void trim_processes() {
  // TODO
}

/**
 * @brief Sets the status of the done jobs so they can be removed
 */
void monitor_jobs() {
  // TODO
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
 * @brief Prints the current stack of jobs in order with uniform format
 */
void print_jobs() {
  // TODO
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
        perror("file doesn't exist");
        return;
      }
      dup2(fd_in, STDIN_FILENO);
      close(fd_in);
      i++;  // next token is file, no need to check
    }
    if (equal(currToken, REDIR_OUT)) {  // ">" command
      tokens[i] = NULL;                 // remove operator from tokens
      // char* outputFile = strdup(nextToken);
      fd_out = open(nextToken, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
      if (fd_out < 0) {
        perror("problem opening file");
        return;
      }
      dup2(fd_out, STDOUT_FILENO);
      close(fd_out);
      i++;  // next token is file, no need to check
    }
    if (equal(currToken, REDIR_ERR)) {  // "2>" command
      tokens[i] = NULL;                 // remove operator from tokens
      fd_err = open(nextToken, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);
      if (fd_err < 0) {
        perror("file doesn't exist");
        return;
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
void executeCommand(char* cmdTokens[], int numToks, char* cmdInit, int bg) {
  pid_t PID = fork();
  if (PID == 0) {
    // inside child process
    redirect(cmdTokens, numToks);
    execvp(cmdTokens[0], cmdTokens);
    fprintf(stderr, "BAD COMMAND\n");  // child not supposed to get here
    _exit(1);
  } else if (PID > 0) {
    // TODO: inside parent process

    wait(NULL);
    printf("\nchild process finished\n");
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
 * @param arg_count1 numbers of args of cmd1
 * @param cmd2 original command
 * @param arg_count2 numbers of args of cmd2
 * @param args string for printing purposes
 * @param bg background toggle for setting wait
 */
void execute_pipe(char* cmd1[],
                  int arg_count1,
                  char* cmd2[],
                  int arg_count2,
                  char* args,
                  int bg) {
  // TODO
}

/**
 * @brief Processes and validates input, splits command into pipe if necessary,
 * executes commands
 *
 * @param initCmd original command string
 */
void process(char* inputCmd) {
  if (inputCmd == NULL)
    return;

  // a copy of input command to mess around with
  char* cmdCopy = strdup(inputCmd);

  char* args[MAX_ARGS];
  int pipeIndex = -1;
  int background = 0;
  char* token;

  int numArgs = 0;
  // parses input command string to get args
  while ((token = strtok_r(cmdCopy, " ", &cmdCopy))) {
    if (equal(token, PIPE)) {  // check if command has a pipe
      pipeIndex = numArgs;
    }
    args[numArgs] = token;  // append token to command array
    numArgs++;
  }
  args[numArgs] = NULL;  // null terminate args

  // TODO: check if last character is & in order to send to background

  // TODO: check if input is built-in shell commands (BG, FG, JOBS, EXIT)

  // handles piping
  if (pipeIndex > 0) {
    // TODO: do command before and after pipe
  } else {
    // execute regular command
    executeCommand(args, numArgs, inputCmd, background);
  }
}

/**
 * @brief Handles interrupt command
 */
void sig_int() {
  kill(-1 * top->pgid, SIGKILL);

  // removes current running process from stack
  if (top->prevProcess == NULL) {
    head = NULL;
    top = NULL;
  } else {
    struct process* temp = top->prevProcess;
    temp->nextProcess = NULL;
    top = temp;
  }
}

/**
 * @brief Handles halt command
 */
void sig_tstp() {
  head->status = 1;
  kill(-1 * head->pgid, SIGTSTP);
}

int main() {
  // set signals to custom handlers
  signal(SIGTTOU, SIG_IGN);
  signal(SIGINT, &sig_int);
  signal(SIGTSTP, &sig_tstp);

  tcsetpgrp(0, getpid());

  while (1) {
    char* cmd = readline("# ");
    if (cmd == NULL)
      _exit(0);
    process(cmd);
    // trim_processes();
    // monitor_jobs();
  }
}