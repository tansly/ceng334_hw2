#include "util.h"

#include <curses.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

static int n_ants;
static int n_food;
static int max_seconds;

static void print_usage(char **argv)
{
    fprintf(stderr, "Usage: %s n_ants n_food max_seconds\n", argv[0]);
}

int main(int argc, char **argv)
{
    srand(time(NULL));

    if (argc != 4) {
        print_usage(argv);
        return 1;
    }
    if (sscanf(argv[1], "%d", &n_ants) != 1) {
        print_usage(argv);
        return 1;
    }
    if (sscanf(argv[2], "%d", &n_food) != 1) {
        print_usage(argv);
        return 1;
    }
    if (sscanf(argv[3], "%d", &max_seconds) != 1) {
        print_usage(argv);
        return 1;
    }

    //////////////////////////////
    // Fills the grid randomly to have something to draw on screen.
    // Empty spaces have to be -.
    // You should get the number of ants and foods from the arguments 
    // and make sure that a food and an ant does not get placed at the same cell.
    // You must use putCharTo() and lookCharAt() to access/change the grid.
    // You should be delegating ants to separate threads
    int i,j;
    for (i = 0; i < GRIDSIZE; i++) {
        for (j = 0; j < GRIDSIZE; j++) {
            putCharTo(i, j, '-');
        }
    }
    int a,b;
    for (i = 0; i < 5; i++) {
        do {
            a = rand() % GRIDSIZE;
            b = rand() % GRIDSIZE;
        }while (lookCharAt(a,b)!= '-');
        putCharTo(a, b, 'P');
    }
    for (i = 0; i < 5; i++) {
        do {
            a = rand() % GRIDSIZE;
            b = rand() % GRIDSIZE;
        }while (lookCharAt(a,b) != '-');
        putCharTo(a, b, '1');
    }
    for (i = 0; i < 5; i++) {
        do {
            a = rand() % GRIDSIZE;
            b = rand() % GRIDSIZE;
        }while (lookCharAt(a,b) != '-');
        putCharTo(a, b, 'o');
    }
    for (i = 0; i < 5; i++) {
        do {
            a = rand() % GRIDSIZE;
            b = rand() % GRIDSIZE;
        }while (lookCharAt(a,b) != '-');
        putCharTo(a, b, 'S');
    }
    for (i = 0; i < 5; i++) {
        do {
            a = rand() % GRIDSIZE;
            b = rand() % GRIDSIZE;
        }while (lookCharAt(a,b) != '-');
        putCharTo(a, b, '$');
    }
    //////////////////////////////

    // you have to have following command to initialize ncurses.
    startCurses();
    
    // You can use following loop in your program. But pay attention to 
    // the function calls, they do not have any access control, you 
    // have to ensure that.
    char c;
    while (TRUE) {
        drawWindow();
        
        c = 0;
        c = getch();

        if (c == 'q' || c == ESC) break;
        if (c == '+') {
            setDelay(getDelay()+10);
        }
        if (c == '-') {
            setDelay(getDelay()-10);
        }
        if (c == '*') {
            setSleeperN(getSleeperN()+1);
        }
        if (c == '/') {
            setSleeperN(getSleeperN()-1);
        }
        usleep(DRAWDELAY);
        
        // each ant thread have to sleep with code similar to this
        //usleep(getDelay() * 1000 + (rand() % 5000));
    }
    
    // do not forget freeing the resources you get
    endCurses();
    
    return 0;
}
