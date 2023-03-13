#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdbool.h>

#define ALPHABET_COUNT 26 //Letters in the alphabet
#define FILE_MAX 100

void checkArgc(int argc); //Check quantity of arguments
void childProcess(int fd[2], char* data, int index); //Child process functionality
void sigchldHandler(int sig); //SIGCHLD handler

int pipes[FILE_MAX][2]; //Store pipes
pid_t childPids[FILE_MAX]; //Store corresponding PID
bool terminatedChildren[FILE_MAX]; //Store which children are finished

int main(int argc, char *argv[]) {
    checkArgc(argc); //Check if there is a proper amount of arguments
    signal(SIGCHLD, sigchldHandler); //Register signal handler

    //Iterate for ever argument given
    for (int i = 0; i < argc - 1; i++) {
        char* filename = argv[i + 1]; //Load file name

        //Create pipe and store in global variable
        if (pipe(pipes[i]) == -1) {
            fprintf(stderr, "Could not create pipe\n");
            exit(1);
        }

        pid_t pid = fork(); //Fork a child process
        if (pid == -1) {
            //Unsuccessful fork
            fprintf(stderr, "Could not create fork\n");
            exit(1);
        } else if (pid == 0) {
            //Child process
            childProcess(pipes[i], filename, i);
        } else {
            //Parent process
            childPids[i] = pid;  //Save child PID for signal handler
            if (strcmp("SIG", filename) == 0) {
                //SIG passed in
                close(pipes[i][1]);
                close(pipes[i][0]);
                kill(pid, SIGINT); // Send SIGINT to child process
                sleep(2);
            }
        }
    }
    //Loop until all processes are complete
    while (true) {
        int num = 0;
        //Iterate through all flags
        for (int i = 0; i < argc - 1; i++) {
            if (terminatedChildren[i]) {
                num++;
            }
        }
        //Check no more child processes
        if (num == argc - 1) {
            break;
        }
    }
    return 0;
}

//Check if argument quantity is sufficient
void checkArgc(int argc) {
    if (argc < 2) {
        //Not enough arguments
        fprintf(stderr, "ERROR: NO ARGUMENTS\n");
        exit(1);
    }
    else if (argc - 1 > FILE_MAX) {
        fprintf(stderr, "ERROR: TOO MANY ARGUMENTS\n");
        exit(1);
    }
}

//Custom signal handler for SIGCHLD
void sigchldHandler(int sig) {
    pid_t pid; //declare pid
    int status = 0; //initialize status
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        int index = -1; //set index to one
        for (int i = 0; i < FILE_MAX; i++) {
            //PID is valid
            if (childPids[i] == pid) {
                index = i; //set new index
                break;
            }
        }
        printf("PID: %d - Caught\n", pid);
        terminatedChildren[index] = true; //Set terminate flag to true
        if (index == -1) {
            //Invalid signal
            fprintf(stderr, "CHILD PROCESS %d NOT FOUND\n", pid);
            close(pipes[index][1]); //Close write end of pipe
            close(pipes[index][0]); //Close the reading end of the pipe
            signal(SIGCHLD, sigchldHandler); //Reinstall the signal handler
            return;
        }
        else if (WIFSIGNALED(status)) {
            printf("PID: %d - Killed by Signal %d (%s)\n", pid, WTERMSIG(status), strsignal(WTERMSIG(status)));
            close(pipes[index][1]); //Close write end of pipe
            close(pipes[index][0]); //Close the reading end of the pipe
            signal(SIGCHLD, sigchldHandler); //Reinstall the signal handler
        }
        else {
            int *histogram = (int*)calloc(ALPHABET_COUNT, sizeof(int)); //Allocate the histogram array
            int rbytes = read(pipes[index][0], histogram, ALPHABET_COUNT * sizeof(int)); //Read the histogram from the pipe
            if (rbytes != ALPHABET_COUNT * sizeof(int)) {
                //Bytes do not match up
                fprintf(stderr, "PID: %d - Histogram Incomplete\n", pid);
                free(histogram); //Free the histogram array
                close(pipes[index][1]); //Close write end of pipe
                close(pipes[index][0]); //Close the reading end of the pipe
                signal(SIGCHLD, sigchldHandler); //Reinstall the signal handler
                return;
            }
            close(pipes[index][0]); //Close the reading end of the pipe
            close(pipes[index][1]); //Close write end of pipe

            char output_filename[20]; //Declare filename
            snprintf(output_filename, 20, "file%d.hist", pid); //Create filename

            //write only, create if doesn't exist, if it does exist contents should be truncated, specify file permission owner: R+W, others: R
            int output_file = open(output_filename, O_WRONLY | O_CREAT | O_TRUNC, 0644); 
            
            if (output_file == -1) {
                //Open did not work
                fprintf(stderr, "Could not open file for writing\n");
                close(pipes[index][1]); //Close the writing end of pipe
                close(pipes[index][0]); //Close the reading end of the pipe
                signal(SIGCHLD, sigchldHandler);
                exit(1);
            }
            char buf[256];
            for (int i = 0; i < ALPHABET_COUNT; i++) {
                int n = snprintf(buf, sizeof(buf), "%c %d\n", 'a' + i, histogram[i]);
                if (write(output_file, buf, n) == -1) {
                    fprintf(stderr, "Error writing to file\n");
                    close(output_file);
                    close(pipes[index][1]);
                    close(pipes[index][0]);
                    signal(SIGCHLD, sigchldHandler);
                    exit(1);
                }
            }
            close(output_file); //Close the output file
            free(histogram); //Free the histogram array
        }
        signal(SIGCHLD, sigchldHandler); //Reinstall the signal handler
    }
}

//Child process functionality
void childProcess(int fd[2], char* filename, int index) {
    signal(SIGINT, SIG_DFL); //Set SIGINT disposition to default
    int input_file = open(filename, O_RDONLY); //Open the input file for reading
    if (input_file == -1) {
        //File can not open
        fprintf(stderr, "Could not open file %s\n", filename); //error message
        close(fd[1]);
        exit(1);
    }

    //Determine the size of the file
    off_t size = lseek(input_file, 0, SEEK_END);
    lseek(input_file, 0, SEEK_SET);

    //Allocate the data array
    char *data = (char*)malloc((size + 1) * sizeof(char)); //add 1 for null terminator

    //Read the file into the data array
    ssize_t rbytes = read(input_file, data, size);
    if (rbytes != size) {
        //Bytes do not match up
        fprintf(stderr, "Error reading file\n");
        close(fd[1]);
        exit(1);
    }
    data[size] = '\0'; //Add null terminator

    close(input_file); //Close the input file

    int *histogram = (int*)calloc(ALPHABET_COUNT, sizeof(int)); //Allocate the histogram array
    for (int i = 0; i < size; i++) {
        char c = data[i]; //Store char
        if (isalpha(c)) {
            c = tolower(c); //Treat upper- and lower-case forms of the same letter the same
            histogram[c - 'a']++; //Increment the count of the corresponding letter
        }
    }

    ssize_t wbytes = write(fd[1], histogram, ALPHABET_COUNT * sizeof(int)); //Send the histogram through the pipe
    if (wbytes != ALPHABET_COUNT * sizeof(int)) {
        //Bytes do not match-up
        fprintf(stderr, "Failed to write results\n");
        close(fd[1]);
        exit(1);
    }
    sleep(10 + 2 * index); //Child sleep

    close(fd[1]); //Close the writing end of the pipe
    free(data); //Free data
    free(histogram); //Free the histogram array
    exit(0); //Process exit
}
