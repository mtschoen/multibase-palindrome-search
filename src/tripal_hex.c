/* tripal_hex: search for numbers palindromic in bases 2, 10 and 16 by generating
 * base-16 palindromes directly (mirror a half) instead of scanning every integer.
 *
 * For hex length L the free half has h = ceil(L/2) digits; there are 15*16^(h-1)
 * halves. Summed over L = 1..16 that is about 8.6e9 candidates for all 64-bit
 * values, versus 1.8e19 for a linear scan. Each candidate gets the binary check
 * (bit reverse) and then the decimal check (divide loop).
 *
 * Build:  clang -O3 -march=native -o tripal_hex tripal_hex.c
 * Run:    tripal_hex [maximum_hex_digits] [threads]       default 16 digits = all of uint64
 *         tripal_hex pairs <maximum_hex_digits>           list base 2 + base 16 palindromes (verification)
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static inline uint64_t reverse_all_nibbles(uint64_t value) {
    value = ((value >> 32) & 0x00000000FFFFFFFFull) | ((value & 0x00000000FFFFFFFFull) << 32);
    value = ((value >> 16) & 0x0000FFFF0000FFFFull) | ((value & 0x0000FFFF0000FFFFull) << 16);
    value = ((value >> 8)  & 0x00FF00FF00FF00FFull) | ((value & 0x00FF00FF00FF00FFull) << 8);
    value = ((value >> 4)  & 0x0F0F0F0F0F0F0F0Full) | ((value & 0x0F0F0F0F0F0F0F0Full) << 4);
    return value;
}

/* Reverse exactly `digit_count` hex digits of `value` (leading zeros included). */
static inline uint64_t reverse_nibbles(uint64_t value, int digit_count) {
    return reverse_all_nibbles(value) >> (64 - 4 * digit_count);
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

/* Build the hex palindrome of length `length` whose leading half (h digits) is `half`. */
static inline uint64_t mirror(uint64_t half, int length) {
    int half_digits = (length + 1) / 2;
    if (length % 2 == 0) {
        return (half << (4 * half_digits)) | reverse_nibbles(half, half_digits);
    }
    /* odd length: the middle digit is the last digit of half and is not repeated */
    return (half << (4 * (half_digits - 1))) | reverse_nibbles(half >> 4, half_digits - 1);
}

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

struct worker_arguments {
    int thread_index;
    int thread_count;
    int maximum_length;
    int pairs_only;          /* verification mode: report base 2 + base 16 pairs, skip base 10 */
    uint64_t candidates;
    uint64_t found_count;
};

static void search(struct worker_arguments *arguments) {
    for (int length = 1; length <= arguments->maximum_length; length++) {
        int half_digits = (length + 1) / 2;
        uint64_t half_begin = (half_digits == 1) ? 1 : (1ull << (4 * (half_digits - 1)));
        uint64_t half_end = 1ull << (4 * half_digits);
        uint64_t span = half_end - half_begin;
        uint64_t chunk = span / (uint64_t)arguments->thread_count;
        uint64_t start = half_begin + chunk * (uint64_t)arguments->thread_index;
        uint64_t end = (arguments->thread_index == arguments->thread_count - 1)
                           ? half_end : start + chunk;
        for (uint64_t half = start; half < end; half++) {
            uint64_t value = mirror(half, length);
            arguments->candidates++;
            if (!is_palindrome_base2(value)) continue;
            if (!arguments->pairs_only && !is_palindrome_base10(value)) continue;
            arguments->found_count++;
            print_hit(value);
        }
    }
}

#ifdef _WIN32
static DWORD WINAPI worker_entry(LPVOID parameter) { search((struct worker_arguments *)parameter); return 0; }
#else
static void *worker_entry(void *parameter) { search((struct worker_arguments *)parameter); return NULL; }
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
    int pairs_only = 0;
    int argument_index = 1;
    if (argc > 1 && strcmp(argv[1], "pairs") == 0) { pairs_only = 1; argument_index = 2; }
    int maximum_length = (argc > argument_index) ? atoi(argv[argument_index]) : 16;
    int thread_count = (argc > argument_index + 1) ? atoi(argv[argument_index + 1]) : hardware_thread_count();
    if (maximum_length < 1 || maximum_length > 16) { fprintf(stderr, "maximum_hex_digits must be 1..16\n"); return 1; }
    if (thread_count < 1) thread_count = 1;

    printf("searching hex palindromes up to %d digits with %d threads%s\n",
           maximum_length, thread_count, pairs_only ? " (pairs mode: base 2 + base 16 only)" : "");
    fflush(stdout);

    struct worker_arguments *arguments = calloc((size_t)thread_count, sizeof *arguments);
    thread_handle *threads = calloc((size_t)thread_count, sizeof *threads);
    clock_t wall_start = clock();
    for (int index = 0; index < thread_count; index++) {
        arguments[index].thread_index = index;
        arguments[index].thread_count = thread_count;
        arguments[index].maximum_length = maximum_length;
        arguments[index].pairs_only = pairs_only;
#ifdef _WIN32
        threads[index] = CreateThread(NULL, 0, worker_entry, &arguments[index], 0, NULL);
#else
        pthread_create(&threads[index], NULL, worker_entry, &arguments[index]);
#endif
    }
    uint64_t total_found = 0, total_candidates = 0;
    for (int index = 0; index < thread_count; index++) {
#ifdef _WIN32
        WaitForSingleObject(threads[index], INFINITE);
        CloseHandle(threads[index]);
#else
        pthread_join(threads[index], NULL);
#endif
        total_found += arguments[index].found_count;
        total_candidates += arguments[index].candidates;
    }
    double elapsed = (double)(clock() - wall_start) / CLOCKS_PER_SEC;
    printf("done: %llu hits among %llu hex palindromes in %.1f s\n",
           (unsigned long long)total_found, (unsigned long long)total_candidates, elapsed);
    free(arguments);
    free(threads);
    return 0;
}
