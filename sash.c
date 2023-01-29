#include <fcntl.h>
#include <readline/readline.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <unistd.h>
#include "stdlib.h"
#include "string.h"

typedef struct Commands {
  char* command;
  bool isCommand;
  int inputIndex;
  int outIndex;
  int errIndex;
  char* arguments[2000];
  char* tokenCommand[2000];
  int numArgs;
  int numTokens;
  bool hasInput;
  char* inputFile;
  bool hasOut;
  char* outFile;
  bool hasErr;
  char* errFile;
} Commands;

typedef struct Jobs {
  Commands comms[2];
  int numCom;
  char* str;
  bool fg;
  bool bg;
  bool hasPipe;
  pid_t pgid;
  int jobNumber;
  bool stop;
  bool interr;
  bool finished;
} Jobs;

Jobs jobs[20] = {NULL};
int currentJob = 0;
int nextJob = 0;
int jobNumber = 0;
pid_t currPgid = 0;

void sigHandler(int signum) {
  if (signum == SIGTSTP) {
    if (currPgid != 0 && jobs[currentJob].bg == false) {
      kill((-1 * currPgid), SIGTSTP);
      for (int i = 0; i < 20; i++)
        jobs[i].fg = false;
      jobs[currentJob].fg = true;
      jobs[currentJob].stop = true;
    }
  } else if (signum == SIGINT) {
    if (currPgid != 0 && jobs[currentJob].bg == false)
      jobs[currentJob].interr = true;
    kill((-1 * currPgid), SIGINT);
  }
}

void fgJob() {
  int status;
  int jobsIdx;
  for (jobsIdx = 0; jobsIdx < 20; jobsIdx++) {
    if (jobs[jobsIdx].fg) {
      currentJob = jobsIdx;
      currPgid = jobs[jobsIdx].pgid;
      jobs[jobsIdx].stop = false;
      jobs[jobsIdx].bg = false;
      if (jobs[jobsIdx].str[strlen(jobs[jobsIdx].str) - 1] == '&')
        jobs[jobsIdx].str[strlen(jobs[jobsIdx].str) - 1] = '\0';
      printf("%s\n", jobs[jobsIdx].str);
      kill(-1 * (jobs[jobsIdx].pgid), SIGCONT);
      break;
    }
  }
  waitpid(jobs[currentJob].pgid, &status, WUNTRACED);
  if (WIFEXITED(status)) {
    jobs[currentJob].finished = true;
  } else if (WIFSTOPPED(status)) {
    if (WSTOPSIG(status) == SIGTSTP)
      jobs[currentJob].stop = true;
    else if (WSTOPSIG(status) == SIGINT)
      jobs[currentJob].interr = true;
  }
}

void bgJob() {
  int status;
  int jobsIdx;
  for (jobsIdx = 0; jobsIdx < 20; jobsIdx++) {
    if (jobs[jobsIdx].fg && jobs[jobsIdx].bg == false) {
      if (jobsIdx > 0)
        jobs[jobsIdx - 1].fg = false;
      currentJob = jobsIdx;
      jobs[jobsIdx].stop = false;
      currPgid = jobs[jobsIdx].pgid;
      jobs[jobsIdx].bg = true;

      strcat(jobs[jobsIdx].str, " &");
      printf("[%d]+ %s\n", jobs[jobsIdx].jobNumber, jobs[jobsIdx].str);
      kill(-1 * (jobs[jobsIdx].pgid), SIGCONT);
      break;
    }
  }
  waitpid(jobs[currentJob].pgid, &status, WNOHANG);
}

void leftShiftJobs() {
  for (int i = 0; i < 20; i++) {
    // empty pos when pgid == 1
    if (jobs[i].pgid == 1) {
      int nearestFullElement;
      for (nearestFullElement = i; nearestFullElement < 19;
           nearestFullElement++) {
        if (jobs[nearestFullElement].pgid != 1)
          break;
      }
      jobs[i] = jobs[nearestFullElement];
      jobs[nearestFullElement].pgid = 1;
    }
  }
  if (jobs[0].pgid == 1) {
    nextJob = 0;
    currentJob = 0;
    jobNumber = 0;
  } else {
    int jobsIdx;
    for (jobsIdx = 1; jobsIdx < 20; jobsIdx++) {
      if (jobs[jobsIdx].pgid == 1)
        break;
    }
    int maxJobNumber = -1;
    for (int i = 0; i < 20; i++) {
      if (jobs[i].jobNumber > maxJobNumber)
        maxJobNumber = jobs[i].jobNumber;
    }
    jobNumber = maxJobNumber;
    nextJob = jobsIdx;
    currentJob = jobsIdx - 1;
  }
}

void freeJobs(const int* arr) {
  for (int arrInd = 0; arrInd < 20; arrInd++) {
    if (arr[arrInd] != -1) {
      for (int i = 0; i < jobs[arr[arrInd]].numCom; i++) {
        free(jobs[arr[arrInd]].comms[i].command);
        if (jobs[arr[arrInd]].comms[i].arguments > 0) {
          for (int j = 0; j < jobs[arr[arrInd]].comms[i].numArgs; j++) {
            free(jobs[arr[arrInd]].comms[i].arguments[j]);
          }
        }
        if (jobs[arr[arrInd]].comms[i].hasInput) {
          free(jobs[arr[arrInd]].comms[i].inputFile);
          jobs[arr[arrInd]].comms[i].hasInput = false;
        }
        if (jobs[arr[arrInd]].comms[i].hasOut) {
          free(jobs[arr[arrInd]].comms[i].outFile);
          jobs[arr[arrInd]].comms[i].hasOut = false;
        }
        if (jobs[arr[arrInd]].comms[i].hasErr) {
          free(jobs[arr[arrInd]].comms[i].errFile);
          jobs[arr[arrInd]].comms[i].hasErr = false;
        }
        jobs[arr[arrInd]].hasPipe = false;
        for (int j = 0; jobs[arr[arrInd]].comms[i].numArgs; j++) {
          jobs[arr[arrInd]].comms[i].arguments[j] = (char*)NULL;
        }
      }
      free(jobs[arr[arr[arrInd]]].str);
      jobs[arr[arrInd]].fg = false;
      jobs[arr[arrInd]].bg = false;
      jobs[arr[arrInd]].pgid = 1;
      jobs[arr[arrInd]].jobNumber = 0;
      jobs[arr[arrInd]].interr = false;
      jobs[arr[arrInd]].stop = false;
      jobs[arr[arrInd]].finished = false;
    }
  }
  leftShiftJobs();
}

void accessJobs() {
  int finishedJobs[20];
  for (int i = 0; i < 20; i++)
    finishedJobs[i] = -1;
  int finishedJobsIdx = 0;
  for (int jobsIdx = 0; jobsIdx < 20; jobsIdx++) {
    if (jobs[jobsIdx].finished) {
      finishedJobs[finishedJobsIdx] = jobsIdx;
      finishedJobsIdx++;
    }
    if (jobs[jobsIdx].bg) {
      int status;
      int ret = waitpid(-1 * jobs[jobsIdx].pgid, &status, WNOHANG | WUNTRACED);
      if (ret != 0 && WIFEXITED(status) && ret != -1) {
        // most recent command
        if (jobs[jobsIdx].fg)
          printf("[%d]+ Done %s\n", jobs[jobsIdx].jobNumber, jobs[jobsIdx].str);
        else
          printf("[%d]- Done %s\n", jobs[jobsIdx].jobNumber, jobs[jobsIdx].str);
        jobs[jobsIdx].finished = true;
        finishedJobs[finishedJobsIdx] = jobsIdx;
        finishedJobsIdx++;
      }
    }
  }

  freeJobs(finishedJobs);

  if (jobs[currentJob].bg == true) {
    int i = currentJob;
    while (jobs[i].bg == true && i > 0) {
      jobs[i].fg = false;
      i--;
    }
    if (jobs[i].bg == false)
      jobs[i].fg = true;
  } else {
    for (int i = 0; i < 20; i++) {
      jobs[i].fg = false;
    }
    jobs[currentJob].fg = true;
  }
}

void printJobs() {
  int interruptedJobs[20];
  int interruptedJobsIdx = 0;
  for (int i = 0; i < 20; i++)
    interruptedJobs[i] = -1;
  for (int jobsIdx = 0; jobsIdx < 20; jobsIdx++) {
    if (jobs[jobsIdx].pgid != 1) {
      if (jobs[jobsIdx].stop == true) {
        if (jobs[jobsIdx].fg)
          printf("[%d] + Stopped %s\n", jobs[jobsIdx].jobNumber,
                 jobs[jobsIdx].str);
        else
          printf("[%d] - Stopped %s\n", jobs[jobsIdx].jobNumber,
                 jobs[jobsIdx].str);
      } else if (jobs[jobsIdx].interr == true) {
        interruptedJobs[interruptedJobsIdx] = jobsIdx;
        interruptedJobsIdx++;
      } else if (jobs[jobsIdx].finished != true) {
        if (jobs[jobsIdx].fg)
          printf("[%d] + Running %s\n", jobs[jobsIdx].jobNumber,
                 jobs[jobsIdx].str);
        else
          printf("[%d] - Running %s\n", jobs[jobsIdx].jobNumber,
                 jobs[jobsIdx].str);
      }
    }
  }
  freeJobs(interruptedJobs);
}

void redirect(int index) {
  if (jobs[currentJob].comms[index].hasInput) {
    int fd_in =
        open(jobs[currentJob].comms[index].inputFile, O_CREAT | O_RDONLY);
    dup2(fd_in, STDIN_FILENO);
    close(fd_in);
  }
  if (jobs[currentJob].comms[index].hasOut) {
    int fd_out =
        open(jobs[currentJob].comms[index].outFile, O_CREAT | O_WRONLY, 0777);
    dup2(fd_out, STDOUT_FILENO);
    close(fd_out);
  }
  if (jobs[currentJob].comms[index].hasErr) {
    int fd = creat(jobs[currentJob].comms[index].errFile, 0644);
    if (fd != -1) {
      dup2(fd, STDERR_FILENO);
      close(fd);
    }
  }
}

void executeCommand() {
  int pgid = fork();
  if (pgid == 0) {
    setpgid(0, 0);
    redirect(0);
    if (execvp(jobs[currentJob].comms[0].command,
               jobs[currentJob].comms[0].arguments) == -1)
      exit(9);
  } else {
    currPgid = pgid;
    jobs[currentJob].pgid = pgid;
    int status;
    if (!jobs[currentJob].bg) {
      waitpid(pgid, &status, WUNTRACED);
      if (WIFEXITED(status))
        jobs[currentJob].finished = true;
      else if (WIFSTOPPED(status)) {
        if (WSTOPSIG(status) == SIGTSTP)
          jobs[currentJob].stop = true;
        else if (WSTOPSIG(status) == SIGINT)
          jobs[currentJob].interr = true;
      }
    } else {
      waitpid(pgid, &status, WNOHANG);
    }
  }
}

void execute2Commands() {
  int pipefd[2];
  int status, pid_1, pid_2;
  pipe(pipefd);
  pid_1 = fork();
  if (pid_1 != 0) {
    currPgid = pid_1;
    jobs[currentJob].pgid = pid_1;

    int count = 0;
    while (count < 2) {
      if (!jobs[currentJob].bg)
        waitpid(-1 * pid_1, &status, WUNTRACED);
      else
        waitpid(-1 * pid_1, &status, WNOHANG);

      if (status == 9)
        count++;
      if (WIFEXITED(status)) {
        jobs[currentJob].finished = true;
        count++;
      } else if (WIFSTOPPED(status)) {
        if (WSTOPSIG(status) == SIGTSTP)
          jobs[currentJob].stop = true;
        else if (WSTOPSIG(status) == SIGINT)
          jobs[currentJob].interr = true;
      }
    }
  } else {
    setpgid(0, 0);
    close(pipefd[0]);               /* Close unused read end */
    dup2(pipefd[1], STDOUT_FILENO); /* Make output go to pipe */
    redirect(0);

    printf(jobs[currentJob].comms[0].arguments);
    if (execvp(jobs[currentJob].comms[0].command,
               jobs[currentJob].comms[0].arguments) == -1) {
      exit(9);
    }
  }

  pid_2 = fork();
  if (pid_2 == 0) {
    setpgid(0, pid_1);
    close(pipefd[1]);              /* Close unused write end */
    dup2(pipefd[0], STDIN_FILENO); /* Get input from pipe */
    redirect(1);

    if (execvp(jobs[currentJob].comms[1].command,
               jobs[currentJob].comms[1].arguments) == -1)
      exit(9);
  }

  close(pipefd[0]);
  close(pipefd[1]);

  waitpid(-1, &status, 0);
  waitpid(-1, &status, 0);
}

void tokenize(char* input) {
  char *cl_copy, *to_free, *token, *save_ptr;
  cl_copy = to_free = strdup(input);

  /*Array that holds a maximum of up to two comms. This corresponds
  to one job*/
  Jobs job = {NULL};
  int cmdIndex = 0;

  job.comms[0].isCommand = true;
  job.comms[1].isCommand = true;
  jobs[nextJob].numCom = 1;

  // Step 1: Gather data about the input
  while ((token = strtok_r(cl_copy, " ", &save_ptr))) {
    if (job.comms[cmdIndex].isCommand) {
      job.comms[cmdIndex].command = strdup(token);
      job.comms[cmdIndex].arguments[job.comms[cmdIndex].numArgs] =
          strdup(token);
      job.comms[cmdIndex].numArgs++;
      job.comms[cmdIndex].isCommand = false;
    } else if (strcmp(token, "<") == 0) {
      job.comms[cmdIndex].hasInput = true;
      job.comms[cmdIndex].inputIndex = job.comms[cmdIndex].numTokens + 1;
    } else if (strcmp(token, ">") == 0) {
      job.comms[cmdIndex].hasOut = true;
      job.comms[cmdIndex].outIndex = job.comms[cmdIndex].numTokens + 1;
    } else if (strcmp(token, "2>") == 0) {
      job.comms[cmdIndex].hasErr = true;
      job.comms[cmdIndex].errIndex = job.comms[cmdIndex].numTokens + 1;
    } else if (strcmp(token, "|") == 0) {
      job.hasPipe = true;
      job.numCom++;
      cmdIndex++;
    } else if (strcmp(token, "&") == 0)
      job.bg = true;
    else if (job.comms[cmdIndex].numTokens != job.comms[cmdIndex].inputIndex &&
             job.comms[cmdIndex].numTokens != job.comms[cmdIndex].outIndex &&
             job.comms[cmdIndex].numTokens != job.comms[cmdIndex].errIndex) {
      job.comms[cmdIndex].arguments[job.comms[cmdIndex].numArgs] =
          strdup(token);
      job.comms[cmdIndex].numArgs++;
    }
    if (strcmp(token, "|") != 0) {
      job.comms[cmdIndex].tokenCommand[job.comms[cmdIndex].numTokens] =
          strdup(token);
      job.comms[cmdIndex].numTokens++;
    }

    cl_copy = NULL;
  }

  for (int i = 0; i < cmdIndex + 1; i++) {
    if (job.comms[i].hasInput)
      job.comms[i].inputFile =
          strdup(job.comms->tokenCommand[job.comms[cmdIndex].inputIndex]);
    if (job.comms[i].hasOut)
      job.comms[i].outFile =
          strdup(job.comms->tokenCommand[job.comms[cmdIndex].outIndex]);
    if (job.comms[i].hasErr)
      job.comms[i].errFile =
          strdup(job.comms->tokenCommand[job.comms[cmdIndex].errIndex]);
  }

  jobs[nextJob] = job;

  if (nextJob == 0)
    jobs[nextJob].fg = true;
  jobs[nextJob].jobNumber = ++jobNumber;
  jobs[nextJob].str = strdup(input);
  free(to_free);

  currentJob = nextJob;
  nextJob++;
}

void jobSetup() {
  for (int i = 0; i < 20; i++)
    jobs[i].pgid = 1;
}

int main() {
  jobSetup();
  char* input;

  input = readline("# ");
  while (input) {
    signal(SIGINT, sigHandler);
    signal(SIGTSTP, sigHandler);

    if (input == NULL)
      break;
    if (strlen(input) > 0) {
      if (strcmp(input, "bg") == 0)
        bgJob();
      else if (strcmp(input, "fg") == 0)
        fgJob();
      else if (strcmp(input, "jobs") == 0) {
        accessJobs();
        printJobs();
      } else {
        tokenize(input);
        if (!jobs[currentJob].hasPipe)
          executeCommand();
        else
          execute2Commands();
      }
      accessJobs();
    }
    input = readline("# ");
  }
}