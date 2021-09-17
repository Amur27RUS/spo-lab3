#include "terminalSettings.h"

//Чтобы программа не останавливалась при считывании пользовательского ввода.
//Подробнее: https://habr.com/ru/post/124789/

//Первая функция для перевода в неканонический режим:
struct termios set_keypress() {
    struct termios new_settings, stored_settings;

    tcgetattr(0,&stored_settings);

    new_settings = stored_settings;

    new_settings.c_lflag &= (~ICANON & ~ECHO);
    new_settings.c_cc[VTIME] = 0;
    new_settings.c_cc[VMIN] = 1;

    tcsetattr(0,TCSANOW,&new_settings);
    return stored_settings;
}

//Вторая функция для возвращения в первоначальное состояние:
void reset_keypress(struct termios stored_settings) {
    tcsetattr(0,TCSANOW,&stored_settings);
}
