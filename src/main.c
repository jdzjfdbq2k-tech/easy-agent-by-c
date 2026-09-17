#include <stdio.h>
#include <stdlib.h>

#include "env.h"
#include "llm.h"

int main(void) {
    if (!load_env(".env")) {
        printf("Failed to load .env\n");
        return 1;
    }

    char input[2048];

    printf("You: ");
    fgets(input, sizeof(input), stdin);

    char *answer = call_llm(input);

    if (!answer) {
        printf("LLM call failed\n");
        return 1;
    }

    printf("AI: %s\n", answer);

    free(answer);
    return 0;
}
