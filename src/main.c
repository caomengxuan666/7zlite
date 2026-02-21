#include "../include/7zlite.h"
#include <stdio.h>
#include <stdlib.h>

/* CLI function */
extern int zlite_cli_main(int argc, char **argv);

int main(int argc, char **argv) {
    return zlite_cli_main(argc, argv);
}