#include <stdio.h>

int main() {
    printf("stdout buffer size = %d\n", stdout->_IO_buf_end - stdout->_IO_buf_base);
    return 0;
}