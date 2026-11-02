/* tripal128: search all 128-bit integers for palindromes in bases 2, 10 and 16.
 *
 * Only numbers that are already palindromic in BOTH base 2 and base 16 are
 * generated. For a number with hex_length hex digits whose leading digit has
 * leading_bits (1..4) significant bits, two involutions act on bit positions:
 *   H: hex palindrome    (nibble j <-> nibble hex_length-1-j, same offset)
 *   B: binary palindrome (bit q <-> bit N-1-q, N = 4*(hex_length-1)+leading_bits)
 * Bits in one orbit of <H, B> must be equal; orbits touching the zero padding
 * above the leading digit are forced to zero. Union-find over at most 128
 * positions yields the free orbits. Candidates are enumerated by Gray code so
 * each step flips one orbit mask. Candidates then get a first-digit versus
 * last-digit decimal prefilter and finally the full decimal palindrome check.
 *
 * Build:  clang -O3 -march=native -o tripal128 tripal128.c
 * Run:    tripal128 [maximum_hex_digits] [threads]      default 32 digits = all of 128 bits
 *         tripal128 pairs <maximum_hex_digits>          list base 2 + base 16 palindromes
 *         tripal128 selftest                        exercise the decimal routines
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

typedef unsigned __int128 uint128;

#define MAXIMUM_HEX_DIGITS 32
#define MAXIMUM_BITS 128
#define MAXIMUM_GROUPS (MAXIMUM_HEX_DIGITS * 4)

/* ---------- 128-bit helpers ---------- */

static inline int count_leading_zeros_128(uint128 value) {
    uint64_t high = (uint64_t)(value >> 64);
    uint64_t low = (uint64_t)value;
    if (high != 0) return __builtin_clzll(high);
    return 64 + __builtin_clzll(low);
}

static uint128 power10[40];   /* 10^0 .. 10^38 */

static void initialize_power10(void) {
    power10[0] = 1;
    for (int index = 1; index <= 38; index++) power10[index] = power10[index - 1] * 10;
}

/* exponent such that 10^exponent <= value < 10^(exponent+1); value must be nonzero */
static inline int decimal_exponent(uint128 value) {
    int bits = 128 - count_leading_zeros_128(value);
    int exponent = (bits * 1233) >> 12;          /* floor(bits * log10(2)), may be one too high */
    if (exponent > 38) exponent = 38;
    while (exponent > 0 && value < power10[exponent]) exponent--;
    while (exponent < 38 && value >= power10[exponent + 1]) exponent++;
    return exponent;
}

/* Cheap reject: first decimal digit must equal last decimal digit. */
static inline int decimal_ends_match(uint128 value) {
    uint64_t high = (uint64_t)(value >> 64);
    uint64_t low = (uint64_t)value;
    unsigned last_digit = (unsigned)((6 * (high % 10) + low % 10) % 10);   /* 2^64 mod 10 == 6 */
    int exponent = decimal_exponent(value);
    if (exponent == 0) return 1;
    uint128 lower_bound = (uint128)last_digit * power10[exponent];
    return value >= lower_bound && value < lower_bound + power10[exponent];
}

/* Writes least-significant digit first; returns digit count. */
static int decimal_digits(uint128 value, unsigned char *digits) {
    const uint64_t chunk = 10000000000000000000ull;   /* 10^19 */
    int length = 0;
    if (value == 0) { digits[0] = 0; return 1; }
    uint128 remaining = value;
    while (remaining != 0) {
        uint128 quotient = remaining / chunk;
        uint64_t rest = (uint64_t)(remaining - quotient * chunk);
        for (int index = 0; index < 19; index++) {
            digits[length++] = (unsigned char)(rest % 10);
            rest /= 10;
        }
        remaining = quotient;
    }
    while (length > 1 && digits[length - 1] == 0) length--;
    return length;
}

static int is_palindrome_base10_128(uint128 value) {
    unsigned char digits[64];
    int length = decimal_digits(value, digits);
    for (int index = 0; index < length / 2; index++) {
        if (digits[index] != digits[length - 1 - index]) return 0;
    }
    return 1;
}

static void print_hit(uint128 value) {
    unsigned char digits[64];
    int length = decimal_digits(value, digits);
    char decimal[65];
    for (int index = 0; index < length; index++) decimal[index] = (char)('0' + digits[length - 1 - index]);
    decimal[length] = 0;

    char binary[129];
    int bits = 128 - count_leading_zeros_128(value);
    for (int index = 0; index < bits; index++) {
        binary[index] = (char)('0' + (unsigned)((value >> (bits - 1 - index)) & 1));
    }
    binary[bits] = 0;

    uint64_t high = (uint64_t)(value >> 64);
    uint64_t low = (uint64_t)value;
    if (high != 0) printf("%s  hex=%llX%016llX  bin=%s\n", decimal, (unsigned long long)high, (unsigned long long)low, binary);
    else printf("%s  hex=%llX  bin=%s\n", decimal, (unsigned long long)low, binary);
    fflush(stdout);
}

/* ---------- orbit groups ---------- */

struct group {
    int hex_length;
    int leading_bits;
    int free_count;              /* orbits other than the leading one */
    uint128 lead_mask;              /* orbit containing the top bit, always set */
    uint128 masks[MAXIMUM_BITS];
    uint64_t count;              /* 2^free_count */
};

static int find_root(int *parent, int position) {
    while (parent[position] != position) {
        parent[position] = parent[parent[position]];
        position = parent[position];
    }
    return position;
}

static void unite(int *parent, int first, int second) {
    first = find_root(parent, first);
    second = find_root(parent, second);
    if (first != second) parent[first] = second;
}

/* Returns 0 when no number exists for this (hex_length, leading_bits). */
static int build_group(int hex_length, int leading_bits, struct group *group) {
    int bit_length = 4 * (hex_length - 1) + leading_bits;
    int padded_length = 4 * hex_length;
    int parent[MAXIMUM_BITS];
    for (int position = 0; position < padded_length; position++) parent[position] = position;

    for (int position = 0; position < padded_length; position++) {
        int nibble = position / 4;
        int offset = position % 4;
        unite(parent, position, 4 * (hex_length - 1 - nibble) + offset);
    }
    for (int position = 0; position < bit_length; position++) {
        unite(parent, position, bit_length - 1 - position);
    }

    unsigned char forced_zero[MAXIMUM_BITS] = {0};
    for (int position = bit_length; position < padded_length; position++) {
        forced_zero[find_root(parent, position)] = 1;
    }

    int lead_root = find_root(parent, bit_length - 1);
    if (forced_zero[lead_root]) return 0;

    group->hex_length = hex_length;
    group->leading_bits = leading_bits;
    group->free_count = 0;
    group->lead_mask = 0;
    for (int root = 0; root < bit_length; root++) {
        if (find_root(parent, root) != root || forced_zero[root]) continue;
        uint128 mask = 0;
        for (int position = 0; position < bit_length; position++) {
            if (find_root(parent, position) == root) mask |= (uint128)1 << position;
        }
        if (root == lead_root) group->lead_mask = mask;
        else group->masks[group->free_count++] = mask;
    }
    group->count = 1ull << group->free_count;
    return 1;
}

/* ---------- threaded search ---------- */

static struct group groups[MAXIMUM_GROUPS];
static int group_count;

struct worker_arguments {
    int thread_index;
    int thread_count;
    int pairs_only;
    uint64_t candidates;
    uint64_t found_count;
};

static void search(struct worker_arguments *arguments) {
    for (int group_index = 0; group_index < group_count; group_index++) {
        const struct group *group = &groups[group_index];
        uint64_t begin = (uint64_t)((uint128)group->count * (uint64_t)arguments->thread_index / (uint64_t)arguments->thread_count);
        uint64_t end = (uint64_t)((uint128)group->count * (uint64_t)(arguments->thread_index + 1) / (uint64_t)arguments->thread_count);
        if (begin >= end) continue;

        uint64_t index = begin;
        uint64_t gray = index ^ (index >> 1);
        uint128 value = group->lead_mask;
        for (int bit = 0; bit < group->free_count; bit++) {
            if ((gray >> bit) & 1) value ^= group->masks[bit];
        }

        for (;;) {
            arguments->candidates++;
            if (arguments->pairs_only) {
                arguments->found_count++;
                print_hit(value);
            } else if (decimal_ends_match(value) && is_palindrome_base10_128(value)) {
                arguments->found_count++;
                print_hit(value);
            }
            index++;
            if (index >= end) break;
            value ^= group->masks[__builtin_ctzll(index)];   /* Gray code step */
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

/* ---------- self test of the decimal routines ---------- */

static int is_palindrome_base10_64(uint64_t value) {
    uint64_t reversed = 0, remaining = value;
    while (remaining != 0) { reversed = reversed * 10 + remaining % 10; remaining /= 10; }
    return reversed == value;
}

static int selftest(void) {
    int failures = 0;
    for (uint64_t value = 1; value < (1ull << 20); value++) {
        int expected = is_palindrome_base10_64(value);
        if (is_palindrome_base10_128(value) != expected) { failures++; printf("full mismatch at %llu\n", (unsigned long long)value); }
        if (expected && !decimal_ends_match(value)) { failures++; printf("prefilter rejected palindrome %llu\n", (unsigned long long)value); }
    }
    for (int exponent = 1; exponent <= 37; exponent++) {
        uint128 palindrome = power10[exponent] + 1;                       /* 1 0...0 1 */
        uint128 not_palindrome = power10[exponent] + 2;                   /* 1 0...0 2 */
        uint128 sevens = 7 * power10[exponent] + 7;                       /* 7 0...0 7 */
        if (!is_palindrome_base10_128(palindrome) || !decimal_ends_match(palindrome)) { failures++; printf("missed 10^%d+1\n", exponent); }
        if (is_palindrome_base10_128(not_palindrome) || decimal_ends_match(not_palindrome)) { failures++; printf("accepted 10^%d+2\n", exponent); }
        if (!is_palindrome_base10_128(sevens)) { failures++; printf("missed 7*10^%d+7\n", exponent); }
        if (decimal_exponent(power10[exponent]) != exponent || decimal_exponent(power10[exponent] - 1) != exponent - 1) { failures++; printf("exponent wrong near 10^%d\n", exponent); }
    }
    uint128 maximum = ~(uint128)0;   /* 340282366920938463463374607431768211455, 39 digits, not a palindrome */
    if (is_palindrome_base10_128(maximum) || decimal_exponent(maximum) != 38) { failures++; printf("maximum value mishandled\n"); }
    printf("selftest: %d failures\n", failures);
    return failures == 0 ? 0 : 1;
}

int main(int argc, char **argv) {
    initialize_power10();
    if (argc > 1 && strcmp(argv[1], "selftest") == 0) return selftest();

    int pairs_only = 0;
    int argument_index = 1;
    if (argc > 1 && strcmp(argv[1], "pairs") == 0) { pairs_only = 1; argument_index = 2; }
    int maximum_length = (argc > argument_index) ? atoi(argv[argument_index]) : MAXIMUM_HEX_DIGITS;
    int thread_count = (argc > argument_index + 1) ? atoi(argv[argument_index + 1]) : hardware_thread_count();
    if (maximum_length < 1 || maximum_length > MAXIMUM_HEX_DIGITS) { fprintf(stderr, "maximum_hex_digits must be 1..%d\n", MAXIMUM_HEX_DIGITS); return 1; }
    if (thread_count < 1) thread_count = 1;

    uint64_t total_candidates = 0;
    for (int hex_length = 1; hex_length <= maximum_length; hex_length++) {
        for (int leading_bits = 1; leading_bits <= 4; leading_bits++) {
            if (build_group(hex_length, leading_bits, &groups[group_count])) {
                total_candidates += groups[group_count].count;
                group_count++;
            }
        }
    }
    printf("searching base 2 + base 16 palindromes up to %d hex digits: %d groups, %llu candidates, %d threads%s\n",
           maximum_length, group_count, (unsigned long long)total_candidates, thread_count,
           pairs_only ? " (pairs mode)" : "");
    fflush(stdout);

    struct worker_arguments *arguments = calloc((size_t)thread_count, sizeof *arguments);
    thread_handle *threads = calloc((size_t)thread_count, sizeof *threads);
    clock_t wall_start = clock();
    for (int index = 0; index < thread_count; index++) {
        arguments[index].thread_index = index;
        arguments[index].thread_count = thread_count;
        arguments[index].pairs_only = pairs_only;
#ifdef _WIN32
        threads[index] = CreateThread(NULL, 0, worker_entry, &arguments[index], 0, NULL);
#else
        pthread_create(&threads[index], NULL, worker_entry, &arguments[index]);
#endif
    }
    uint64_t total_found = 0, visited = 0;
    for (int index = 0; index < thread_count; index++) {
#ifdef _WIN32
        WaitForSingleObject(threads[index], INFINITE);
        CloseHandle(threads[index]);
#else
        pthread_join(threads[index], NULL);
#endif
        total_found += arguments[index].found_count;
        visited += arguments[index].candidates;
    }
    double elapsed = (double)(clock() - wall_start) / CLOCKS_PER_SEC;
    printf("done: %llu hits among %llu candidates visited in %.1f s\n",
           (unsigned long long)total_found, (unsigned long long)visited, elapsed);
    free(arguments);
    free(threads);
    return 0;
}
