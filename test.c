#include <stdio.h>

int main() {
    char name[50];

    printf("Hello! What's your name? ");
    scanf("%49s", name);  // limit input to avoid buffer overflow

    printf("Nice to meet you, %s!\n", name);

    return 0;
}