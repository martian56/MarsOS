#ifndef MARS_OS_USERMODE_H
#define MARS_OS_USERMODE_H

int usermode_prepare(void);
int usermode_is_prepared(void);
void usermode_enter_test(void);

#endif