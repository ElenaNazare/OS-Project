#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>

#define MAX_USERS 10
#define TOTAL_TIME 10
#define MAX_NUM_PROCESSES 10
#define MAX_USERNAME_LENGTH 50

// Exemplu de input pentru "inoput.txt"
/*
3
USR1 20 3 proc1 proc2 proc3
USR2 30 3 proc4 proc5 proc6
USR3 50 3 proc7 proc8 proc9
*/

// Global variables
int NUM_USERS;
pid_t child_pid;
int global_current_process[MAX_USERS], global_current_user;
bool change_user = false;

// Global variable to keep track of the current process index for each user
int current_process_index[MAX_USERS];
volatile pid_t current_process_pid[MAX_USERS];

struct User {
    char* name;
    int weight;
    int num_processes;
    char** processes;
    int* proc_statuses; // status of each process
    // 0 - the process hasn't started,
    // 1 - the process needs to be continued,
    // 2 - the process has finished its execution
};

volatile struct User users[MAX_USERS];

/*volatile struct User users[NUM_USERS] = {
        {"USR1", 20, 3,(char*[]){"proc1", "proc2", "proc3"},(int[]){0,0,0}},
        {"USR2", 30, 3,(char*[]){"proc4", "proc5", "proc6"},(int[]){0,0,0}},
        {"USR3", 50, 3,(char*[]){"proc7", "proc8", "proc9"},(int[]){0,0,0}}
};*/

void freeUser(volatile struct User* user) {
    free(user->name);
    for (int i = 0; i < user->num_processes; ++i) {
        free(user->processes[i]);
    }
    free(user->processes);
    free(user->proc_statuses);
}

void readInputFromFile(FILE* file, volatile struct User* user) {
    user->name = malloc(MAX_USERNAME_LENGTH);
    fscanf(file, "%s %d %d", user->name, &user->weight, &user->num_processes);

    user->processes = malloc(user->num_processes * sizeof(char*));
    user->proc_statuses = malloc(user->num_processes * sizeof(int));

    for (int j = 0; j < user->num_processes; ++j) {
        user->processes[j] = malloc(MAX_NUM_PROCESSES);
        fscanf(file, "%s", user->processes[j]);
        user->proc_statuses[j] = 0; // initializing process statuses to 0
    }
}

void execute_process(char* user, char* process) {
    char cwd[PATH_MAX], path[PATH_MAX];

    if (getcwd(cwd, sizeof(cwd)) == NULL) { // gets the current directory
        perror("getcwd");
        _exit(1);
    }
    snprintf(path, sizeof(path), "%s/%s", cwd, process);

    printf("Executing %s: %s\n", user, process);
    fflush(stdout);

    char* program_args[] = {path, NULL};

    execv(program_args[0], program_args); // executes the process

    perror("execv");
    _exit(1);
}

void alarm_handler(int signum) {

    change_user = true;
    kill(child_pid, SIGSTOP); // suspends the current process execution because the user's time is up

    if (users[global_current_user].proc_statuses[global_current_process[global_current_user]] == 0) {
        users[global_current_user].proc_statuses[global_current_process[global_current_user]] = 1;
        // if the process wasn't executed before, and has been suspended,
        // we mark it as 1 because it needs to be continued.
    }
}

bool check_user_done(){
    // check if current user has ran all of its processes, so that we don't execute them again
    for(int j=0;j<users[global_current_user].num_processes;j++)
        if(users[global_current_user].proc_statuses[j]!=2)
            return false;
    return true;
}

bool is_finished_exec(){
    // returns true when all users have finished executing all their processes
    for(int i=0; i< NUM_USERS;i++)
        for(int j=0;j<users[i].num_processes;j++)
            if(users[i].proc_statuses[j]!=2)
                return false;
    return true;
}

void user_weighted_round_robin() {
    while (!is_finished_exec()) {
        for (int i = 0; i < NUM_USERS; ++i , change_user = false) {
            global_current_user = i;

            volatile struct User* current_user = &users[i];
            int user_execution_time = floor((current_user->weight * TOTAL_TIME) / 100);
            // find how much time the user has

            signal(SIGALRM,alarm_handler);
            if(!check_user_done()){
                printf("User Execution Time: %d\n",user_execution_time);
            }
            alarm(user_execution_time); // after the time is up for the user, SIGALRM is sent

            if(current_process_pid[i]!=-1 && users[global_current_user].proc_statuses[global_current_process[global_current_user]]==1){ //check if process has been stopped
                printf("Executing %s: %s\n", users[global_current_user].name, users[global_current_user].processes[global_current_process[global_current_user]]);
                if(kill(current_process_pid[i],SIGCONT)==-1){ // try to resume it
                    perror("kill");
                }
                child_pid=current_process_pid[i];
                int status;
                pid_t stopped_pid = waitpid(current_process_pid[i], &status, WUNTRACED);
                if (WIFSTOPPED(status)) {
                    current_process_pid[i] = stopped_pid;
                    // if process was stopped, mark its status as 1 (needs to be continued)
                    users[global_current_user].proc_statuses[global_current_process[global_current_user]]=1;
                } else if (WIFEXITED(status)){
                    // if process was stopped, mark its status as 2 (has finished execution)
                    users[global_current_user].proc_statuses[global_current_process[global_current_user]]=2;
                }
            }


            // execute processes for the current user starting from the current process index
            for (int j = current_process_index[i]; j < current_user->num_processes && !change_user; ++j) {
                global_current_process[global_current_user] = j;

                if (!check_user_done()) {
                    // make a child for each process so that it can become the process to be executed using execv
                    pid_t pid = fork();
                    child_pid = pid;

                    if (pid == 0) {
                        // child process
                        execute_process(current_user->name,
                                        current_user->processes[global_current_process[global_current_user]]);
                        _exit(0);
                    } else if (pid > 0) {
                        // parent process
                        int status;
                        pid_t stopped_pid = waitpid(pid, &status, WUNTRACED);
                        // WUNTRACED to wait for when the child finishes because of SIGSTOP received
                        // we use status to determine whether it was stopped or just finished executing normally

                        if (stopped_pid == -1) {
                            perror("waitpid");
                            exit(1);
                        }


                        if (WIFSTOPPED(status)) {
                            current_process_pid[i] = stopped_pid;
                            // if process was stopped, mark its status as 1
                            users[global_current_user].proc_statuses[global_current_process[global_current_user]] = 1;
                        } else if (WIFEXITED(status)) {
                            // if process was stopped, mark its status as 2
                            users[global_current_user].proc_statuses[global_current_process[global_current_user]] = 2;
                        }

                        // update the current process index for the next user
                        current_process_index[i] = (current_process_index[i] + 1);
                    } else {
                        perror("fork");
                        _exit(1);
                    }

                }
            }
            if(!check_user_done()) {
                printf("\n");
            }
        }
    }
}


int main(int argc, char *argv[], char *envp[]) {
    FILE* file = fopen("input.txt", "r");
    if (file == NULL) {
        perror("Error opening file");
        return 1;
    }

    fscanf(file, "%d", &NUM_USERS);

    for (int i = 0; i < NUM_USERS; ++i) {
        readInputFromFile(file, &users[i]);
    }

    fclose(file);

    // initialising data
    for(int i=0; i<NUM_USERS; i++){
        global_current_process[i]=-1;
        current_process_pid[i]=-1;
    }

    user_weighted_round_robin();

    // free allocated memory
    for (int i = 0; i < NUM_USERS; ++i) {
        freeUser(&users[i]);
    }
    return 0;
}