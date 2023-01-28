/**
 * EE461S - Project 1
 * Yash Shell
 *
 */

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
#define MAX_ARGS 100
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
  int state;   // 0=running; 1=stopped; 2=done
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
  proc->state = 0;
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
  struct process* cur = head;
  while (cur != NULL) {
    // if current process is done
    if (cur->state == 2) {
      // if only 1 element
      if (cur->nextProcess == NULL) {
        // print finished process if it was running in background
        if (cur->isBackground == 1) {
          printf(DONE_F, cur->job_num, CUR_M, DONE, cur->text);
        }

        // if only 1 element, set both pointers to null
        if (cur->prevProcess == NULL) {
          head = NULL;
          top = NULL;
        }
        // if topmost node in stack, remove and reset top
        else {
          top = cur->prevProcess;
          cur->prevProcess->nextProcess = NULL;
        }
      }

      else {
        // print finised process if it was running in background
        if (cur->isBackground == 1) {
          printf(DONE_F, cur->job_num, BAK_M, DONE, cur->text);
        }
        // if bottom element is done, remove and set bottom to next up
        if (cur->prevProcess == NULL) {
          head = cur->nextProcess;
        }
        // remove node if in middle of stack
        else {
          cur->prevProcess->nextProcess = cur->nextProcess;
          cur->nextProcess->prevProcess = cur->prevProcess;
        }
      }
    }
    if (cur != NULL)
      cur = cur->nextProcess;
  }
}

/**
 * @brief Sets the status of the done jobs so they can be removed
 */
void monitor_jobs() {
  struct process* cur = head;
  while (cur != NULL) {
    if (waitpid(-1 * cur->pgid, NULL, WNOHANG | WUNTRACED) != 0 &&
        cur->state == 0) {
      cur->state = 2;
    }
    cur = cur->nextProcess;
  }
}

/**
 * @brief Sends current stopped process to background
 */
void send_to_back() {
  if (head->state == 2) {
    kill(-1 * head->pgid, SIGCONT);
    head->state = 0;
  }
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
  struct process* cur = head;
  while (cur != NULL) {
    if (cur->state == 2) {
      if (cur->nextProcess == NULL) {
        printf(DONE_F, cur->job_num, CUR_M, DONE, cur->text);
      } else {
        printf(DONE_F, cur->job_num, BAK_M, DONE, cur->text);
      }
    } else if (cur->state == 1) {
      if (cur->nextProcess == NULL) {
        printf(GEN_F, cur->job_num, CUR_M, STOPPED, cur->text);
      } else {
        printf(GEN_F, cur->job_num, BAK_M, STOPPED, cur->text);
      }
    } else {
      if (cur->nextProcess == NULL) {
        printf(GEN_F, cur->job_num, CUR_M, RUNNING, cur->text);
      } else {
        printf(GEN_F, cur->job_num, BAK_M, RUNNING, cur->text);
      }
    }
    cur = cur->nextProcess;
  }
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
 * @param args parsed list of args
 * @param arg_count overall number of args (not including &)
 */
void set_operators(char* args[], int arg_count) {
  int f_in = -1;
  int f_out = -1;
  int f_err = -1;

  for (int i = 0; i < arg_count - 1; i++) {
    // if  ">" redirect
    if (strcmp(args[i], REDIR_OUT) == 0) {
      if (i >= arg_count - 1) {
        continue;
      }
      args[i] = NULL;

      char* file = strdup(args[i + 1]);
      f_out = open(file, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);

      if (f_out < 0) {
        perror("cannot open file");
        return;
      } else {
        dup2(f_out, 1);
        close(f_out);
        ++i;
      }
    }
    // if  "<" redirect
    if (strcmp(args[i], REDIR_IN) == 0) {
      if (i >= arg_count - 1) {
        continue;
      }

      args[i] = NULL;

      char* file = args[i + 1];
      f_in = open(file, O_RDONLY, S_IRWXU);

      if (f_in < 0) {
        perror("cannot find file");
        return;
      } else {
        dup2(f_in, 0);
        close(f_in);
        ++i;
      }
    }
    // if  "2>" redirect
    if (strcmp(args[i], REDIR_ERR) == 0) {
      if (i >= arg_count - 1) {
        continue;
      }
      args[i] = NULL;

      char* file = args[i + 1];
      f_err = open(file, O_WRONLY | O_CREAT | O_TRUNC, S_IRWXU);

      if (f_err < 0) {
        perror("cannot find file");
        return;
      } else {
        dup2(f_err, 2);
        close(f_err);
        ++i;
      }
    }
  }
}

/**
 * @brief Executes input command using execvp
 *
 * @param cmd parsed command list of strings
 * @param arg_count number of args
 * @param args original command string for printing purposes
 * @param bg background toggle for setting wait
 */
void execute_cmd(char* cmd[], int arg_count, char* args, int bg) {
  int pid = fork();
  if (pid < 0) {
    perror("fork failed");
    _exit(1);
  } else if (pid == 0) {
    // setpgid(0,0);
    set_operators(cmd, arg_count);
    execvp(cmd[0], cmd);
  } else {
    add_process(args, pid, -1, bg);
    if (!bg) {
      wait(NULL);
    }
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
  int pfd[2];
  int cpid1, cpid2;
  pipe(pfd);
  cpid1 = fork();
  if (cpid1 == 0) {
    // setpgid(0,0)
    dup2(pfd[1], 1);
    close(pfd[0]);
    set_operators(&cmd1[0], arg_count1);
    execvp(cmd1[0], cmd1);
  }
  cpid2 = fork();
  if (cpid2 == 0) {
    // setpgid(0, cpid1)
    dup2(pfd[0], 0);
    close(pfd[1]);
    set_operators(&cmd2[0], arg_count2);
    execvp(cmd2[0], cmd2);
  }
  if (cpid1 < 0 || cpid2 < 0) {
    perror("fork failed");
    _exit(1);
  } else {
    add_process(args, cpid1, cpid2, bg);
    if (!bg) {
      wait(NULL);
    }
  }
}

/**
 * @brief Processes and validates input, splits command into pipe if necessary,
 * executes commands
 *
 * @param cmd original command string
 */
void process(char* cmd) {
  if (cmd == NULL)
    return;

  char* orig = strdup(cmd);

  char* args[MAX_ARGS];
  int arg_count = 0;
  int pipe_address = -1;
  int background = 0;

  // parses input command string to get args
  args[arg_count] = strtok(cmd, " ");
  if (args[arg_count] == NULL)
    return;
  while (args[arg_count] != NULL) {
    // append next token to command array
    args[++arg_count] = strtok(NULL, " ");

    // check if command is a pipe
    if (args[arg_count] != NULL && strcmp(args[arg_count], PIPE) == 0) {
      pipe_address = arg_count;
    }
  }
  args[++arg_count] = NULL;

  // check if last character is & in order to send to background
  if (arg_count >= 2 && args[arg_count - 2] != NULL &&
      strcmp(args[arg_count - 2], BACKGROUND) == 0) {
    background = 1;
    args[arg_count - 2] = NULL;
  }

  if (strcmp(args[0], FG) == 0) {
    bring_to_front();
    return;
  } else if (strcmp(args[0], BG) == 0) {
    send_to_back();
    return;
  } else if (strcmp(args[0], JOBS) == 0) {
    print_jobs();
    return;
  } else if (strcmp(args[0], EXIT) == 0) {
    // kill_all_processes();
    _exit(0);
  }

  // handles piping
  if (pipe_address > 0) {
    int arg_num = arg_count - 1;

    // populate cmd1 with first command before pipe
    char* cmd1[pipe_address + 1];
    for (int i = 0; i < pipe_address; i++) {
      cmd1[i] = args[i];
    }
    cmd1[pipe_address] = NULL;

    // populate cmd2 with command after pipe
    char* cmd2[arg_num - pipe_address];
    for (int i = 0; i < arg_num - pipe_address - 1; i++) {
      cmd2[i] = args[pipe_address + 1 + i];
    }
    cmd2[arg_num - pipe_address - 1] = NULL;

    // execute pipe command
    execute_pipe(cmd1, pipe_address, cmd2, arg_num - pipe_address - 1, orig,
                 background);
  } else {
    // execute regular command
    execute_cmd(args, arg_count, orig, background);
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
  head->state = 1;
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
    if (!cmd)
      _exit(0);
    process(cmd);
    trim_processes();
    monitor_jobs();
  }
}