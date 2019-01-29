#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <pty.h>
#include <ctype.h>
#include <signal.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#define DEFAULT_TTY "./tty1_ext"
#define PROGRAM "calamares"
#define VERSION "1.7"

static int debug = 0;
static int nlcr = 0;
static int raw = 1;     /* enabled by default for ISAM apps */
static int wait_tty = 0;

static int error_printf(const char *format, ...)
{
    va_list args;
    int chars_printed = 0;

    va_start(args, format);
    fprintf(stderr, "[" PROGRAM "] ");
    chars_printed = vprintf(format, args);
    va_end(args);

    return chars_printed;
}

/* safe read: see Linux System Programming book */
/* Only read the bytes readily available, even if less than count */
static ssize_t safe_read_available(int fd, void *buf, ssize_t count, char *errstr)
{
    ssize_t ret = 0;

    while ((ret = read (fd, buf, count)) != 0) {

        if (ret == -1) {
            if (errno == EINTR)
                /* just restart */
                continue;

            /* error */
            error_printf("%s (%s) (fd=%d)\r\n", errstr, strerror(errno), fd);
            break;
        }

        break;
    }

    return ret;
}

/* safe write: see Linux System Programming book */
static ssize_t safe_write(int fd, const void *buf, size_t count, char *errstr)
{
    ssize_t ret = 0;
    ssize_t len = count;

    while (len != 0 && (ret = write (fd, buf, len)) != 0) {

        if (ret == -1) {
            if (errno == EINTR)
                continue;

            error_printf("%s (%s) (fd=%d)\r\n", errstr, strerror(errno), fd);
            break;
        }

        len -= ret;
        buf += ret;
    }

    if (ret == -1)
        return ret;
    else
        return count - len;
}

static int forward_data(int from, int to)
{
    char buffer[64];
    int num_read, num_write;

    num_read = safe_read_available(from, buffer, sizeof(buffer), "reading data failed");
    if (num_read == 0) {
        /* other end is not available anymore */
        error_printf("no data available in fd %d, channel has closed.\n", from);
        return -1;
    } else if (num_read < 0) {
        return -1;
    }

    if (debug) {
        int i;
        fprintf(stderr, "[debug] read %d bytes from %d:", num_read, from);
        for (i = 0; i < num_read; i++)
            fprintf(stderr, " %1$02x|%1$c", buffer[i]);
        fprintf(stderr, "\n");
    }

    num_write = safe_write(to, buffer, num_read, "writing data failed");
    if (num_write < 0) {
        return -1;
    }

    return 0;
}

/*
 * NOTE: the following code related to tty settings is the same as used in
 * the ISAM shell (KRNLMISC/rs_shell_reborn.c). When you change something
 * here, consider whether the same change is needed in ISAM.
 */
static void install_signal_handler(void);
static int handlers_installed = 0;
static struct termios orig_termios;
static int tty_setup(int fd)
{
    struct termios new_termios;

    /*
     * Here we setup the standard input tty settings. We have to do it here,
     * because we need to restore them when calamares exits (so that the Linux
     * shell continues to work as expected.)
     * The tty settings of the external tty are set in the ISAM build.
     */

    if(tcgetattr(fd, &orig_termios) < 0) {
        error_printf("tcgetattr failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    new_termios = orig_termios;

    /*
     * Before messing with the settings, install a signal handler that restores
     * the original settings. Note: only do this the first time, not after
     * every suspend/resume cycle.
     */
    if (handlers_installed == 0) {
        install_signal_handler();
        handlers_installed = 1;
    }

    if (raw) {
        /* Set raw mode: the ISAM application will handle all terminal characters */
        cfmakeraw(&new_termios);
    }

    /* But: handle Ctrl-C / Ctrl-Z in Linux instead of ISAM (interrupt, quit, suspend) */
    new_termios.c_lflag |= ISIG;

    if (nlcr) {
        /* Enable implementation-defined output processing */
        new_termios.c_oflag |= OPOST;
        /* Map NL to CR-NL on output */
        new_termios.c_oflag |= ONLCR;
    }

    if (tcsetattr (fd, TCSANOW, &new_termios) < 0) {
        error_printf("tcsetattr failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int tty_restore(int fd)
{
    if (tcsetattr (fd, TCSAFLUSH, &orig_termios) < 0) {
        error_printf("tcsetattr failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static struct sigaction action;
static struct sigaction orig_int_action;
static struct sigaction orig_term_action;
static struct sigaction orig_quit_action;
static struct sigaction orig_stop_action;
static struct sigaction orig_cont_action;
static void signal_handler(int signum)
{
    switch (signum) {
    case SIGTSTP:
        (void)tty_restore(STDIN_FILENO);
        /* Re-install our continue handler */
        sigaction(SIGCONT, &action, NULL);
        /* Call default handler */
        sigaction (signum, &orig_stop_action, NULL);
        raise(signum);
        break;

    case SIGCONT:
        (void)tty_setup(STDIN_FILENO);
        /* Re-install our stop handler */
        sigaction(SIGTSTP, &action, NULL);
        /* Call default handler */
        sigaction (signum, &orig_cont_action, NULL);
        raise(signum);
        break;

    case SIGQUIT:
        (void)tty_restore(STDIN_FILENO);
        sigaction (signum, &orig_quit_action, NULL);
        raise(signum);
        break;
    case SIGINT:
        (void)tty_restore(STDIN_FILENO);
        sigaction (signum, &orig_int_action, NULL);
        raise(signum);
        break;
    case SIGTERM:
        (void)tty_restore(STDIN_FILENO);
        sigaction (signum, &orig_term_action, NULL);
        raise(signum);
        break;
    default:
        break;
        /* mismatch between handler and handler installation */
    }
}

static void install_signal_handler(void)
{
    action.sa_handler = signal_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    /*
     * We catch SIGINT (Ctrl-C), SIGTERM (sent by kill) and SIGQUIT (Ctrl-\).
     * In addition, we handle suspending and resuming this application
     * (SIGTSTP and SIGCONT). In all these cases, we have to take care of
     * restoring the right tty settings.
     */
    sigaction (SIGINT, &action, &orig_int_action);
    sigaction (SIGTERM, &action, &orig_term_action);
    sigaction (SIGQUIT, &action, &orig_quit_action);

    sigaction (SIGTSTP, &action, &orig_stop_action);
    sigaction (SIGCONT, &action, &orig_cont_action);
}

static int verify_tty_file(char *ttyname)
{
    if (wait_tty == 0 && access(ttyname, F_OK) == -1) {
        error_printf("tty not available [%s]\n", ttyname);
        return -1;
    } else {
        error_printf("waiting for tty [%s] ...", ttyname);
        fflush(stdout);
        while (1) {
            if (access(ttyname, F_OK) != -1) {
                fprintf(stderr,". ok\n");
                break;
            } else {
                fprintf(stderr,".");
                sleep(1);
            }
        } /* end while */

        return 0;
    }
}

static int create_pid_file(char *pid_file)
{
    int ret = 0;
    int pid_fd = -1;
    char pid_str[64] = "";
    struct flock pid_lock;

    pid_fd = open(pid_file, O_RDWR | O_CREAT | O_CLOEXEC, S_IRUSR | S_IWUSR);
    if (pid_fd < 0) {
        error_printf("could not open pidfile %s: %s\n", pid_file, strerror(errno));
        return -1;
    }

    pid_lock.l_type = F_WRLCK;
    pid_lock.l_whence = SEEK_SET;
    pid_lock.l_start = 0;
    pid_lock.l_len = 0;

    ret = fcntl(pid_fd, F_SETLK, &pid_lock);
    if (ret < 0) {
        /* could no take lock on pid_file */
        if (errno == EAGAIN || errno == EACCES)
            error_printf("could not lock pidfile %s\n", pid_file);
        else
            error_printf("error lock file: %s\n", strerror(errno));
        close(pid_fd);
        return -1;
    } else {
        /* lock taken, write own pid to pid_file */
        snprintf(pid_str,64,"%ld",(long)getpid());
        if(ftruncate(pid_fd,0)<0)
            return -1;
        ret = write(pid_fd,pid_str,strlen(pid_str));
        if (ret != strlen(pid_str)) {
            error_printf("could not write to pidfile %s: %s\n", pid_file, strerror(errno));
            close(pid_fd);
            return -1;
        } else {
            error_printf("pid file locked, pid [%s]\n", pid_str);
            return 0;
        }
    }
}

static int lock_tty_file(char *ttyname)
{
    int i = 0;
    int len = 0;
    char pid_file[1024] = "";
    char location[1024] = "";
    char *tmp = NULL;

    /* first check on tty file */
    if (verify_tty_file(ttyname) < 0)
        return -1;

    /* get location of pty by tty symlink */
    if ((tmp = realpath(ttyname, NULL)) == NULL) {
        error_printf("error symlink %s: %s\n", ttyname, strerror(errno));
        return -1;
    }

    len = strlen(tmp);
    snprintf(location,512,"%s",tmp);
    free(tmp);

    /* replace '/' with '-' for file name */
    for (i = 0; i < len; i++)
        if (location[i] == '/')
            location[i] = '-';

    /* create pid_file string */
    snprintf(pid_file,512,"/var/run/calamares%s.pid",location);

    return create_pid_file(pid_file);
}

int main(int argc, char *argv[])
{
    int ttyfd;
    char *ttyname = DEFAULT_TTY;
    int c;

    printf("%s version %s\n", PROGRAM, VERSION);

    opterr = 0;
    while ((c = getopt (argc, argv, "t:wdnch")) != -1) {
        switch (c) {
        case 'w':
          wait_tty = 1;
          break;
        case 't':
          ttyname = optarg;
          break;
        case 'd':
          debug = 1;
          break;
        case 'n':
          nlcr = 1;
          break;
        case 'c':
          raw = 0;
          break;
        case 'h':
          printf("Usage: %s [options]\n", argv[0]);
          printf("Options:\n"
                 "    -h        : help (this text)\n"
                 "    -t tty    : specify tty (default: %s)\n"
                 "    -w        : wait for the tty to become available\n"
                 "    -n        : set NL to CR-NL on output\n"
                 "    -c        : use tty in canonical mode (default raw mode)\n"
                 "    -d        : enable debug output\n", DEFAULT_TTY);
          exit(EXIT_SUCCESS);
          break;
        case '?':
          if (optopt == 't')
            fprintf (stderr, "Option -%c requires an argument.\n", optopt);
          else if (isprint (optopt))
            fprintf (stderr, "Unknown option `-%c'.\n", optopt);
          else
            fprintf (stderr,
                     "Unknown option character `\\x%x'.\n",
                     optopt);
          return 1;
        default:
          abort ();
        }
    }

    if (lock_tty_file(ttyname) < 0)
        return EXIT_FAILURE;

    if ((ttyfd = open(ttyname, O_RDWR)) == -1) {
        error_printf("could not open tty %s: %s\n", ttyname, strerror(errno));
        return EXIT_FAILURE;
    }

    if (tty_setup(STDIN_FILENO) != EXIT_SUCCESS)
        return EXIT_FAILURE;

    while(1)
    {
        fd_set read_set;
        int numReady;

        FD_ZERO(&read_set);
        FD_SET(ttyfd,&read_set);
        FD_SET(STDIN_FILENO,&read_set);

        numReady = select(FD_SETSIZE,&read_set,0,0,0);
        if (numReady)
        {
            if (FD_ISSET(STDIN_FILENO,&read_set)) {
                if (0 != forward_data(STDIN_FILENO, ttyfd))
                    break;
            }

            if (FD_ISSET(ttyfd,&read_set)) {
                if (0 != forward_data(ttyfd, STDOUT_FILENO))
                    break;
            }
        }
    }

    (void)tty_restore(STDIN_FILENO);
    close(ttyfd);
    return EXIT_SUCCESS;
}
