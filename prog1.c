/*
Author: Donovan Troshynski
Class: CSCI 4500 Operating Systems, Fall 2018
Program 1
Heavily based on a simple shell program by Stanley Wileman
*/

#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAXLINELEN 100
#define MAXWORDS 16
#define MAXWORDLEN 64

#define STDINERR "Error reading standard input.\n"
#define STDINEOF "End of file before end of line on standard input.\n"
#define NOSUCHFILE "No such file.\n"
#define CANNOTFORK "Cannot fork.\n"
#define CANNOTEXEC "Cannot exec.\n"
#define CANNOTOPEN "Cannot open input file.\n"
#define CANNOTCREATE "Cannot create output file.\n"
#define TOOMANYREDIRECTS "Too many < or > redirections.\n"
#define NOFILENAME "No filename after < or >\n"
#define THEPROCESSTERMINATED "The process terminated "
#define ABNORMALLY "abnormally.\n"
#define NORMALLY "normally with exit status "
#define UNKNOWNOPERAND "Error: unkown command line operand\n"
#define LINETOOLONG "Error: input line is too long.\n"
#define WORDTOOLONG "Error: input word is too long.\n"
#define TOOMANYWORDS "Error: too many word in command line.\n"
#define TOOMANYPIPES "Error: too many pipe symbols; limit is one.\n"
#define NOCOMMANDAFTERPIPE "Error: no command given after pipe symbol.\n"
#define PIPINGERROR "Error: cannot create pipe\n"

/* my definitions */
#define ever (;;)
#define PATH_MAX 64

extern char **environ;

char words[MAXWORDS][MAXWORDLEN+1];	/* words on the command line */
int nwds;			/* number of words in words array */
int ignore[MAXWORDS];		/* ignore[i] != 0 --> ignore words[i] */
char line[MAXLINELEN+1];	/* current input line */
char *eveargs[MAXWORDS+1];	/* pointers to execve arguments */
char *ifn, *ofn;		/* input, output filenames */
char path[PATH_MAX];		/* path to executable file */
int display;			/* should we show process termination status? */
int echo;			/* echo command lines read from a file? */
int verbose;			/* produce verbose output? */
int fromfile;			/* does standard input come from a file? */

int pipeLocation;

/*------------------------------------------------------------------*/
/* Get a line from the standard input. Return 1 on success, or 0 at */
/* end of file. If the line contains only whitespace, ignore it and */
/* get another line. If the line is too long, display a message and */
/* get another line. If read fails, diagnose the error and abort.   */
/* If standard input is a terminal, prompt for each input line.     */
/* If standard input is from a file, do not prompt for input.       */
/*------------------------------------------------------------------*/
int GetLine(void) {
    int n;              /* result of read system call */
    int len;            /* length of input line */
    int gotnb;          /* non-zero when non-whitespace was seen */
    char c;             /* current input character */

    pipeLocation = 0;
    for ever {
        if (!fromfile) {
            /* writes the command line symbol if input is not from a file */
	        write(1,"$ ",2);
        }
        gotnb = len = 0;
        for ever {
            /* loops through and reads each individual character from input */
            n = read(0,&c,1);
            if (n == 0) {
                /* 0 means nothing was read (end of file?) */
                return 0;
            }
            if (n == -1) {
                /* -1 means read error; should not really occur */
		        write(1,STDINERR,strlen(STDINERR));
                _exit(1);
            }
            if (c == '\n') {
                /* end of input */
                break;
            }
            if (len >= MAXLINELEN) {
                /* too many characters, ignore them and skip a full line */
                len++;
                continue;
            }
            if (c != ' ' && c != '\t') {
                /* gotnb is set to a non-zero int if a non-blank or tab character was seen (had something as input) */
                gotnb = 1;
            }

            /* save the character input if all checks passed */
            line[len++] = c;
        }
        if (len >= MAXLINELEN) {
	        write(1,LINETOOLONG,strlen(LINETOOLONG));
            continue;
        }
        if (gotnb == 0) {
            /* line empty; try again */
            continue;
        }

        /* end input string with a null character */
        line[len] = '\0';

        return 1;
    }
}

/*--------------------------------------------------------------*/
/* Identify "words" on an input line from the array 'line'.     */
/* Put a pointer to each null-terminated string representing a  */
/* word in the 'words' array. Store the total number of words   */
/* the global variable 'nwds'. On success, return 1.            */
/*                                                              */
/* If there are too many words (more than MAXWORDS), or if a    */
/* word is too long (more than MAXWORDLEN characters), display  */
/* an appropriate error message to the standard error device    */
/* and return 0.                                                */
/*                                                              */
/* Except for space and tab characters, none of the characters  */
/* in the input line are explicitly tested. It is expected that */
/* the 'line' variable does not include an end of line, but is  */
/* instead terminated by a null byte, as are all C strings.     */
/*--------------------------------------------------------------*/
int lex(void) {
    char *p;				/* current word */
    int gotPipe = 0;

    nwds = 0;
    p = strtok(line," \t");		/* find first word */
    while (p != NULL) {
	    if (strlen(p) > MAXWORDLEN) {
	        write(1,WORDTOOLONG,strlen(WORDTOOLONG));
	        return 0;
	    }
	    if (nwds == MAXWORDS) {
	        write(1,TOOMANYWORDS,strlen(TOOMANYWORDS));
	        return 0;
	    }

        if(!strcmp(p, "exit")) {
            _exit(1);
        }

        /* check for pipe symbol */
        if(!strcmp(p, "|")) {
            if(gotPipe) {
                write(1, TOOMANYPIPES, strlen(TOOMANYPIPES));
                return 0;
            }
            pipeLocation = nwds;
            gotPipe = 1;
        }

        /* save pointer to word */
	    strcpy(words[nwds],p);
        
        /* increment number of words */
	    nwds++;

        /* find next word */
	    p = strtok(NULL," \t");
    }
    return 1;
}

/*---------------------------------------------------------------------------*/
/* If the command identified by words[0] is executable, then put the path to */
/* the executable command in the global "path" variable and return 0.        */
/* If the command is not executable (for whatever reason), return -1.        */
/*---------------------------------------------------------------------------*/
/* The executable command path is determined in the same manner as "normal"  */
/* shells. If word[0] contains a slash -- that is, it is an explicit path -- */
/* then it is used as specified. Otherwise, the command named by words[0] is */
/* sought as the name of an executable file in the directories identified by */
/* the PATH environment variable, a colon-separated list of directory names. */
/*---------------------------------------------------------------------------*/
int execok(void) {
    char *p;

    /* copy of PATH environment variable */
    char *pathenv;

    if (strchr(words[0],'/') != NULL) {
        /* words at 0 (first word) has a slash; assume this is direct path to an executable */
        
        /* set the path/directory as the path to executable */
        strcpy(path,words[0]);

        /* return 0 if ok, else -1 */
        return access(path,X_OK);
    }

    /* copy of path variable */
    pathenv = strdup(getenv("PATH"));

    /* find first dir in PATH */
    p = strtok(pathenv,":");
    while (p != NULL) {
        /* possible executable path */
        strcpy(path,p);

        strcat(path,"/");
        strcat(path,words[0]);
        if (access(path,X_OK) == 0) {
            /* path is executable */

            /* free PATH copy */
            free(pathenv);

            return 0;
        }

        /* check next directory in PATH */
        p = strtok(NULL,":");
    }

    /* no more PATH directories to check, free PATH copy */
    free(pathenv);

    /* return failure indicator */
    return -1;
}

/*-------------------------------------------------------*/
/* Write an 8-bit unsigned value 'val' as a 1-3 digit    */
/* decimal number to the open file with descriptor 'fd'. */
/*-------------------------------------------------------*/
void write8bu(int fd, unsigned char val) {
    int sig = 0;
    char c;

    if (val >= 100) {
        /* get hundreds digit */
	    c = '0' + val / 100;

        /* remove that digit from val */
	    val %= 100;

        /* indicate significance start */
	    sig = 1;

        /* display the digit */
	    write(fd,&c,1);
    }
    if (val >= 10 || sig != 0) {
        /* val greater than or equal to 10 or significance started */

        /* get tens digit */
	    c = '0' + val / 10;

        /* remove that digit from val */
	    val %= 10;

        /* display it */
	    write(fd,&c,1);
    }

    /* always display the unit digit */
    c = '0' + val;
    write(fd,&c,1);
}

/*--------------------------------------------------------------------------*/
/* The main program first checks for the various allowed options. THis is   */
/* a simple implementation, and doesn't include many of the extended option */
/* specification syntax features of many applications.			    */
/*									    */
/* The program then gets an input line, does lexical processing of the line */
/* to identify the words, and then determines if the first word represents  */
/* the name of an executable file.					    */
/*									    */
/* The list of words is then checked for occurrences of "> filename" and/or */
/* "< filename", detecting if they are used more than once. If found, the   */
/* words array entries that contain these words are marked to be ignored,   */
/* by setting the appropriate entries in the ignore array.                  */
/*									    */
/* Pointers to the words that are not ignored are copied to the eveargs     */
/* array for use with the execve system call.				    */
/*									    */
/* A child process is created to execute the command. In the child process, */
/* any input/output redirection requested is set up, and the execve system  */
/* call is used to execute the requested program with the given arguments.  */
/*									    */
/* The parent process waits for the child process to terminate. If the -d   */
/* option was specified, the child's termination status is also displayed.  */
/*									    */
/* Agressive checking for errors is performed in each of the above steps,   */
/* with display of an appropriate message and appropriate subsequent action */
/* taking place.							    */
/*--------------------------------------------------------------------------*/
int main(int argc, char *argv[]) {
    int i, j, status;
    int cpid;
    int rightChild;
    int fail;
    int pipeCommand[2];
    char *leftCommand[MAXWORDS + 1];
    char *rightCommand[MAXWORDS + 1];
    int stdin_copy = dup(0);
    int stdout_copy = dup(1);

    /* equals 1 if input is from a file */
    fromfile = !isatty(0);

    /* verify there's nothing else on the command line */
    if (argc > 1) {
	    write(1,UNKNOWNOPERAND,strlen(UNKNOWNOPERAND));
	    _exit(1);
    }

    /* process commands given inside this program (not options given above) */
    /* GetLine() gets a command line input */
    while (GetLine()) {
        /* separate input command into words */
	    if (!lex()) {
            /* ignore line if it had an error */
	        continue;
        }

        /* runs if the command is not executable */
	    if (execok() != 0) {
	        write(1,CANNOTEXEC,strlen(CANNOTEXEC));
	        continue;
	    }

        /* assume no I/O redirection */
	    ifn = ofn = NULL;

	    for (i=0;i<nwds;i++) {
	        ignore[i] = 0;
        }

        /* check for input redirection */
	    fail = 0;
	    for (i=1;i<nwds;i++) {
            /* checks all legal positions */

	        if (strcmp(words[i],"<") && strcmp(words[i],"<<")) {
                /* skip if not a less-than symbol (input redirection) */
		        continue;
            }

            /* input redirection can't be the last word */
	        if (i == nwds-1) {
		        write(1,NOFILENAME,strlen(NOFILENAME));
		        fail = 1;
		        break;
	        }

            /* already have input redirection? */
	        if (ifn != NULL) {
		        write(1,TOOMANYREDIRECTS,strlen(TOOMANYREDIRECTS));
		        fail = 1;
		        break;
	        }
	        ignore[i] = 1;
	        ignore[i+1] = 1;
	        ifn = words[i+1];
	        i++;
	    }

	    if (fail) {
	        continue;
        }

        /* check for output redirection */
	    fail = 0;
	    for (i=1;i<nwds;i++) {
            /* checks all legal positions */

	        if (ignore[i]) {
                /* skip words in input redirection */
		        continue;
            }

	        if (strcmp(words[i],">") && strcmp(words[i],">>")) {
		        /* skip if not greater-than symbol (output redirection) */
                continue;
            }

            /* verify filename */
	        if (i == nwds-1 || ignore[i+1] != 0) {
		        write(1,NOFILENAME,strlen(NOFILENAME));
		        fail = 1;
		        break;
	        }

            /* already have output redirection? */
	        if (ofn != NULL) {
		        write(1,TOOMANYREDIRECTS,strlen(TOOMANYREDIRECTS));
		        fail = 1;
		        break;
	        }
	        ignore[i] = 1;
	        ignore[i+1] = 1;
	        ofn = words[i+1];
	        i++;
	    }
	    if (fail) {
	        continue;
        }

        if(pipeLocation > 0) {
            if(nwds - 1 > pipeLocation && pipeLocation != 0) {
                /* do nothing */
            } else {
                write(1, NOCOMMANDAFTERPIPE, strlen(NOCOMMANDAFTERPIPE));
                fail = 1;
                break;
            }
        }

        /* builds a list of args for execve */
	    eveargs[0] = words[0];
	    for (i=1,j=1;i<nwds;i++) {
            /* copy all but indirection words */
	        if (ignore[i]) {
                /* word already used? */
		        continue;
            }
	        eveargs[j++] = words[i];
	    }
        /* end of argument list */
	    eveargs[j] = NULL;

        /* print command being run */
	    write(1,"# ",2);
	    for(i=0;;i++) {
		    if (eveargs[i] == NULL) {
                break;
            } else {
		        write(1,eveargs[i],strlen(eveargs[i]));
            }
		    write(1," ",1);
	    }
        write(1,"\n",1);

        /* create pipe if pipe symbol is present */
        if(pipeLocation > 0) {
            int i = 0;
            int x = 0;
            
            if(pipe(pipeCommand) == -1) {
                write(1,PIPINGERROR,strlen(PIPINGERROR));
                continue;
            }

            while(i < pipeLocation) {
                if(eveargs[i] == NULL) {
                    break;
                } else {
                    leftCommand[i] = eveargs[i];
                }
                i++;
            }
            leftCommand[i] = NULL;
            if(i == pipeLocation) {
                i++;
            }
            while(i > pipeLocation) {
                if(eveargs[i] == NULL) {
                    break;
                } else {
                    rightCommand[x] = eveargs[i];
                }
                x++;
                i++;
            }
            rightCommand[x] = NULL;

            /*write(1,"Left: ",6);
            write(1,"'",1);
            for(j = 0;;j++) {
                if (leftCommand[j] == NULL) {
                    break;
                } else {
                    write(1,leftCommand[j],strlen(leftCommand[j]));
                }
                write(1," ",1);
            }
            write(1,"'",1);
            write(1,"\n",1);

            write(1,"Right: ",7);
            write(1,"'",1);
            for(h = 0;; h++){
                if(rightCommand[h] == NULL) {
                    break;
                } else {
                    write(1,rightCommand[h],strlen(rightCommand[h]));
                }
                write(1," ",1);
            }
            write(1,"'",1);
            write(1,"\n",1);*/
        }

        /* create a new process to execute the program */
        /* first child process */
	    cpid = fork();
	    if (cpid == -1) {
            /* creation of child process failed */
	        write(2,CANNOTFORK,strlen(CANNOTFORK));
	        continue;
	    }

	    if (cpid == 0) {
            /* in the child process */

	        /* setup input file descriptor per redirection */
	        if (ifn != NULL) {
                /* open input file */
		        int fd = open(ifn,O_RDONLY);

		        if (fd < 0) {
                    /* open input file failed */
		            write(2,CANNOTOPEN,strlen(CANNOTOPEN));

                    /* child process terminates with failure */
		            _exit(1);
		        }
                /* map stdin to new file descriptor */
		        close(0);
		        dup(fd);
		        close(fd);
	        }

	        /* Setup output file descriptor per redirection. */
	        if (ofn != NULL) {
		        int fd = open(ofn,O_CREAT|O_WRONLY|O_TRUNC,0644);
		        if (fd < 0) {
		            write(2,CANNOTCREATE,strlen(CANNOTCREATE));
		            exit(1);
		        }
		        close(1);
		        dup(fd);
		        close(fd);
	        }

            if(pipeLocation > 0) {
                close(1);
                dup(pipeCommand[1]);
                close(pipeCommand[0]);
                close(pipeCommand[1]);

                execve(path,leftCommand,environ);
                write(1,"Cannot execute left command.\n",29);
            } else {
                execve(path,eveargs,environ);
                write(2,CANNOTEXEC,strlen(CANNOTEXEC));
            }
            exit(1);
	    }

	    /* Parent (shell): wait for the child to terminate. */
	    wait(&status);

        /* create second child process if using pipe */
        /* will execute right side of pipe symbol */
        if(pipeLocation > 0) {
            rightChild = fork();
            if(rightChild == -1) {
                write(1,CANNOTFORK,strlen(CANNOTFORK));
                continue;
            }

            if(rightChild == 0) {
                close(0);
                dup(pipeCommand[0]);
                close(pipeCommand[0]);
                close(pipeCommand[1]);

                execve(path,rightCommand,environ);
                write(1,"Cannot execute right pipe command.\n",35);
                exit(1);
            }

            wait(&status);
        }
        close(pipeCommand[0]);
        close(pipeCommand[1]);
        dup2(stdin_copy,0);
        dup2(stdout_copy,1);
        close(stdin_copy);
        close(stdout_copy);

	    /* Display exit status and reason for termination. */
	    if (display) {
	        write(1,THEPROCESSTERMINATED,strlen(THEPROCESSTERMINATED));

            /* lo-order byte != 0 */
	        if (status & 0xff) {
                /* error if non-zero */
		        write(1,ABNORMALLY,strlen(ABNORMALLY));
            } else {
		        write(1,NORMALLY,strlen(NORMALLY));
		        write8bu(1,(unsigned char)((status >> 8) & 0xff));
		        write(1,".\n",2);
	        }
	    }

    }

    return 0;
}
