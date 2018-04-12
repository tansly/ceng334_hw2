#ifndef UTIL_H
#define UTIL_H

#define ESC 27
#define DRAWDELAY 50000
#define GRIDSIZE 30

void setDelay(int d);
int getDelay();
void setSleeperN(int d);
int getSleeperN();
void putCharTo(int i, int j, char c);
char lookCharAt(int i, int j);
void startCurses();
void endCurses();
void drawWindow();

#endif /* UTIL_H */
