#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>

#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <getopt.h>


static int usage()
{
    fprintf(stderr, "Usage: chmod [OPTION] <MODE> <FILE>\n");
    fprintf(stderr, "  -h, --no-dereference    do not follow symlink\n");
    fprintf(stderr, "  --help                  display this help and exit\n");

    return 10;
}

int chmod_main(int argc, char **argv)
{
    int i;
    int noFollow = 0;
    int fd = 0;
    int ch = 0;
    unsigned int flag =0;
    int help = 0;
    static struct option long_options[] =
        {
            {"help",       no_argument,       0, 'H'},
            {"no-dereference",  no_argument,  0, 'h'}
        };
    /* getopt_long stores the option index here. */
    int option_index = 0;
    while((ch = getopt_long(argc, argv, "Hh",long_options,&option_index)) != -1)
    switch(ch){
        case 'H':
            help = 1;
            break;
        case 'h':
            noFollow = 1;
            break;
        default:
            break;
    }

    if (argc < 3 || help) {
        return usage();
    }

    if (noFollow && argc < 4) {
        return usage();
    }

    if(noFollow) {
        flag = O_NOFOLLOW;
        argc--;
        argv++;
    }
    int mode = 0;
    const char* s = argv[1];
    while (*s) {
        if (*s >= '0' && *s <= '7') {
            mode = (mode<<3) | (*s-'0');
        }
        else {
            fprintf(stderr, "Bad mode\n");
            return 10;
        }
        s++;
    }
    for (i = 2; i < argc; i++) {
        if(((fd = open(argv[i], flag|O_RDONLY )) != -1)||((fd = open(argv[i], flag|O_WRONLY )) != -1)) {
            if (fchmod(fd, mode) < 0){
                fprintf(stderr, "Unable to chmod %s: %s\n", argv[i], strerror(errno));
                close(fd);
                return 10;
            }
            close(fd);
        } else {
            fprintf(stderr, "Unable to open %s: %s\n", argv[i], strerror(errno));
            return 10;
        }
    }
    return 0;
}
