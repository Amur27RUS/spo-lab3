#pragma once
#include <termios.h>

//from https://habr.com/ru/post/124789/

struct termios set_keypress();

void reset_keypress(struct termios stored_settings);
