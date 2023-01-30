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
struct job {
  struct job* prevJob;
  int jobID;
  int status;       // 0=running; 1=stopped; 2=done/completed
  char* jobString;  // original command
  pid_t pgid;       // group id
  pid_t leftChildID;
  pid_t rightChildID;
  int isBackground;  // boolean
  struct job* nextJob;
};

// nodes represting base and top of job stack
struct job* head = NULL;
struct job* top = NULL;

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
void add_process(char* args, int pid1, int pid2, int background) {}

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
void executeCommand(char* cmdTokens[], int numToks, char* cmdInit, int bg) {
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

    waitpid(PID, NULL, WUNTRACED);
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
                        char* initInput,
                        int bg) {
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

  if (p1 < 0 || p2 < 0) {
    printf("Fork failure, returned pid1=%d, pid2=%d\n", p1, p2);
  }
  close(pfd[0]);
  close(pfd[1]);
  waitpid(-1, NULL, /*WNOHANG | */ WUNTRACED);  // wait for one of them to end
  waitpid(-1, NULL, /*WNOHANG | */ WUNTRACED);  // and the other one
  // wait(NULL);
  // wait(NULL);
  printf("returned to main process\n");
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
  int background = 0;
  char* token;

  int numArgs = 0;
  // parses input command string to get args
  while ((token = strtok_r(cmdCopy, " ", &cmdCopy))) {
    args[numArgs] = token;     // append token to command array
    if (equal(token, PIPE)) {  // check if command has a pipe
      pipeIndex = numArgs;     // remembers the location of the pipe
      args[pipeIndex] = NULL;  // null terminates cmd1
    }
    numArgs++;
  }
  args[numArgs] = NULL;  // null terminate args

  // TODO: check if last character is & in order to send to background

  // TODO: check if input is built-in shell commands (BG, FG, JOBS, EXIT)

  if (pipeIndex > 0) {
    // execute piped commmands
    char** cmd1 = args;  // cmd1 starts the same as input but ends at pipeIndex
    char** cmd2 = &args[pipeIndex + 1];  // cmd2 starts after pipeIndex
    executeTwoCommands(cmd1, pipeIndex, cmd2, numArgs - pipeIndex - 1, inputCmd,
                       background);
  } else {
    // execute regular command
    executeCommand(args, numArgs, inputCmd, background);
  }
}

/**
 * @brief Handles interrupt command
 */
void sig_int() {
  // kill(-1 * top->pgid, SIGKILL);

  // // removes current running process from stack
  // if (top->prevJob == NULL) {
  //   head = NULL;
  //   top = NULL;
  // } else {
  //   struct process* temp = top->prevJob;
  //   temp->nextJob = NULL;
  //   top = temp;
  // }
  printf("\npressed ctrl+c, interrupt\n");
}

/**
 * @brief Handles halt command
 */
void sig_tstp() {
  // head->status = 1;
  // kill(-1 * head->pgid, SIGTSTP);
  printf("\npressed ctrl+z, interactive stop\n");
}

int main() {
  // set signals to custom handlers
  signal(SIGTTOU, SIG_IGN);
  signal(SIGINT, sig_int);
  signal(SIGTSTP, sig_tstp);

  // give terminal control to yash by default
  tcsetpgrp(0, getpid());

  while (1) {
    char* cmd = readline("# ");
    if (cmd == NULL)
      _exit(0);
    process(cmd);
    // trim_processes();
    // monitor_jobs();
    free(cmd);
  }
}