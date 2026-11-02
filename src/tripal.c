/* tripal: brute-force search for numbers that are palindromes in base 2, 10 and 16.
 *
 * Build (clang, any platform):  clang -O3 -march=native -o tripal tripal.c
 * Run:   tripal [limit] [threads]     default limit 2^32, threads = hardware count
 *
 * Checks are ordered cheapest-and-most-selective first: binary (bit reverse),
 * then hex (nibble reverse), then decimal (digit loop). Only numbers that pass
 * the binary filter (about 1 in 2^(bits/2)) ever reach the decimal check.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
typedef HANDLE thread_handle;
#else
#include <pthread.h>
#include <unistd.h>
typedef pthread_t thread_handle;
#endif

static inline int is_palindrome_base2(uint64_t value) {
    uint64_t reversed = __builtin_bitreverse64(value) >> __builtin_clzll(value);
    return reversed == value;
}

static inline uint64_t reverse_nibbles(uint64_t value) {
    value = ((value >> 32) & 0x00000000FFFFFFFFull) | ((value & 0x00000000FFFFFFFFull) << 32);
    value = ((value >> 16) & 0x0000FFFF0000FFFFull) | ((value & 0x0000FFFF0000FFFFull) << 16);
    value = ((value >> 8)  & 0x00FF00FF00FF00FFull) | ((value & 0x00FF00FF00FF00FFull) << 8);
    value = ((value >> 4)  & 0x0F0F0F0F0F0F0F0Full) | ((value & 0x0F0F0F0F0F0F0F0Full) << 4);
    return value;
}

static inline int is_palindrome_base16(uint64_t value) {
    int leading_zero_nibbles = __builtin_clzll(value) / 4;
    uint64_t reversed = reverse_nibbles(value) >> (4 * leading_zero_nibbles);
    return reversed == value;
}

static inline int is_palindrome_base10(uint64_t value) {
    uint64_t reversed = 0;
    uint64_t remaining = value;
    while (remaining != 0) {
        reversed = reversed * 10 + remaining % 10;
        remaining /= 10;
    }
    return reversed == value;
}

struct worker_arguments {
    uint64_t start;
    uint64_t end;        /* exclusive */
    uint64_t found_count;
};

static void print_hit(uint64_t value) {
    char binary[65];
    int length = 64 - __builtin_clzll(value);
    for (int index = 0; index < length; index++) {
        binary[index] = (char)('0' + ((value >> (length - 1 - index)) & 1));
    }
    binary[length] = '\0';
    printf("%llu  hex=%llX  bin=%s\n", (unsigned long long)value, (unsigned long long)value, binary);
    fflush(stdout);
}

static void search_range(struct worker_arguments *arguments) {
    for (uint64_t value = arguments->start; value < arguments->end; value++) {
        if (!is_palindrome_base2(value)) continue;
        if (!is_palindrome_base16(value)) continue;
        if (!is_palindrome_base10(value)) continue;
        arguments->found_count++;
        print_hit(value);
    }
}

#ifdef _WIN32
static DWORD WINAPI worker_entry(LPVOID parameter) {
    search_range((struct worker_arguments *)parameter);
    return 0;
}
#else
static void *worker_entry(void *parameter) {
    search_range((struct worker_arguments *)parameter);
    return NULL;
}
#endif

static int hardware_thread_count(void) {
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return (int)info.dwNumberOfProcessors;
#else
    long count = sysconf(_SC_NPROCESSORS_ONLN);
    return count > 0 ? (int)count : 1;
#endif
}

int main(int argc, char **argv) {
    if (argc > 2 && argv[1][0] == 'c') {   /* tripal check N...  : print the three predicates */
        for (int index = 2; index < argc; index++) {
            uint64_t value = strtoull(argv[index], NULL, 0);
            printf("%llu  base2=%d base16=%d base10=%d\n", (unsigned long long)value,
                   is_palindrome_base2(value), is_palindrome_base16(value), is_palindrome_base10(value));
        }
        return 0;
    }
    uint64_t limit = (argc > 1) ? strtoull(argv[1], NULL, 0) : (1ull << 32);
    int thread_count = (argc > 2) ? atoi(argv[2]) : hardware_thread_count();
    if (thread_count < 1) thread_count = 1;

    printf("searching [1, %llu) with %d threads\n", (unsigned long long)limit, thread_count);
    fflush(stdout);

    struct worker_arguments *arguments = calloc((size_t)thread_count, sizeof *arguments);
    thread_handle *threads = calloc((size_t)thread_count, sizeof *threads);
    uint64_t chunk = limit / (uint64_t)thread_count;

    clock_t wall_start = clock();
    for (int index = 0; index < thread_count; index++) {
        arguments[index].start = (index == 0) ? 1 : (uint64_t)index * chunk;
        arguments[index].end = (index == thread_count - 1) ? limit : (uint64_t)(index + 1) * chunk;
#ifdef _WIN32
        threads[index] = CreateThread(NULL, 0, worker_entry, &arguments[index], 0, NULL);
#else
        pthread_create(&threads[index], NULL, worker_entry, &arguments[index]);
#endif
    }

    uint64_t total_found = 0;
    for (int index = 0; index < thread_count; index++) {
#ifdef _WIN32
        WaitForSingleObject(threads[index], INFINITE);
        CloseHandle(threads[index]);
#else
        pthread_join(threads[index], NULL);
#endif
        total_found += arguments[index].found_count;
    }
    double elapsed = (double)(clock() - wall_start) / CLOCKS_PER_SEC;

    printf("done: %llu hits in %.1f s\n", (unsigned long long)total_found, elapsed);
    free(arguments);
    free(threads);
    return 0;
}
