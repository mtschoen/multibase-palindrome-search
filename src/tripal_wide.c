/* tripal_wide: search integers up to 256 bits for palindromes in bases 2, 10 and 16.
 *
 * Same orbit construction as tripal128 (numbers are generated already palindromic
 * in base 2 and base 16; see that file), but values are arrays of 64-bit limbs so
 * the search can run past 128 bits. The decimal prefilter compares the first
 * three decimal digits (recovered from a double of the top bits, with an exact
 * fallback when the estimate sits near a rounding boundary) against the last
 * three decimal digits (recovered from limb residues mod 1000). Survivors get the
 * full decimal palindrome check.
 *
 * Build:  clang -O3 -march=native -o tripal_wide tripal_wide.c
 * Run:    tripal_wide <minimum_hex_digits> <maximum_hex_digits> [threads] [leading_bits]
 *         leading_bits selects which leading-digit widths to search, default "1234":
 *         1 = leading hex digit 1, 2 = digit 3, 3 = digits 5 and 7, 4 = digits 9 and F
 *         tripal_wide pairs <minimum_hex_digits> <maximum_hex_digits>     list base 2 + base 16 palindromes
 *         tripal_wide selftest                                    exercise the decimal routines
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

#define LIMBS 4
#define MAXIMUM_BITS (64 * LIMBS)
#define MAXIMUM_HEX_DIGITS (MAXIMUM_BITS / 4)
#define MAXIMUM_GROUPS (MAXIMUM_HEX_DIGITS * 4)
#define MAXIMUM_DECIMAL_DIGITS 78            /* 2^256 has 78 decimal digits */
#define DIGIT_BUFFER (19 * (MAXIMUM_DECIMAL_DIGITS / 19 + 1))   /* whole 19-digit chunks, 95 */

struct wide { uint64_t limb[LIMBS]; };   /* limb[0] is least significant */

/* ---------- wide helpers ---------- */

static inline int wide_is_zero(const struct wide *value) {
    uint64_t any = 0;
    for (int index = 0; index < LIMBS; index++) any |= value->limb[index];
    return any == 0;
}

static inline int wide_bit_length(const struct wide *value) {
    for (int index = LIMBS - 1; index >= 0; index--) {
        if (value->limb[index] != 0) return 64 * index + 64 - __builtin_clzll(value->limb[index]);
    }
    return 0;
}

static inline void wide_set_bit(struct wide *value, int position) {
    value->limb[position / 64] |= 1ull << (position % 64);
}

static inline int wide_get_bit(const struct wide *value, int position) {
    return (int)((value->limb[position / 64] >> (position % 64)) & 1);
}

static inline void wide_xor(struct wide *value, const struct wide *mask) {
    for (int index = 0; index < LIMBS; index++) value->limb[index] ^= mask->limb[index];
}

/* value = value * multiplier + addend; multiplier and addend are small */
static void wide_multiply_add(struct wide *value, uint64_t multiplier, uint64_t addend) {
    unsigned __int128 carry = addend;
    for (int index = 0; index < LIMBS; index++) {
        unsigned __int128 product = (unsigned __int128)value->limb[index] * multiplier + carry;
        value->limb[index] = (uint64_t)product;
        carry = product >> 64;
    }
}

/* 128 by 64 division with the quotient known to fit in 64 bits */
static inline uint64_t divide_128_by_64(uint64_t high, uint64_t low, uint64_t divisor, uint64_t *remainder) {
#if defined(__x86_64__)
    uint64_t quotient;
    __asm__("divq %4" : "=a"(quotient), "=d"(*remainder) : "a"(low), "d"(high), "r"(divisor));
    return quotient;
#else
    unsigned __int128 current = ((unsigned __int128)high << 64) | low;
    *remainder = (uint64_t)(current % divisor);
    return (uint64_t)(current / divisor);
#endif
}

/* value /= divisor in place; returns the remainder */
static uint64_t wide_divide_small(struct wide *value, uint64_t divisor) {
    uint64_t remainder = 0;
    for (int index = LIMBS - 1; index >= 0; index--) {
        value->limb[index] = divide_128_by_64(remainder, value->limb[index], divisor, &remainder);
    }
    return remainder;
}

/* ---------- decimal routines ---------- */

static double power10_double[MAXIMUM_DECIMAL_DIGITS + 1];
static double limb_scale[LIMBS];                  /* (2^64)^i as double, exact */
static uint64_t limb_weight_mod_1000[LIMBS];      /* (2^64)^i mod 1000 */

static void initialize_tables(void) {
    power10_double[0] = 1.0;
    for (int index = 1; index <= MAXIMUM_DECIMAL_DIGITS; index++) power10_double[index] = power10_double[index - 1] * 10.0;
    limb_scale[0] = 1.0;
    for (int index = 1; index < LIMBS; index++) limb_scale[index] = limb_scale[index - 1] * 18446744073709551616.0;
    limb_weight_mod_1000[0] = 1;
    for (int index = 1; index < LIMBS; index++) limb_weight_mod_1000[index] = (limb_weight_mod_1000[index - 1] * 616) % 1000;   /* 2^64 mod 1000 */
}

static inline unsigned wide_mod_1000(const struct wide *value) {
    uint64_t sum = 0;
    for (int index = 0; index < LIMBS; index++) sum += (value->limb[index] % 1000) * limb_weight_mod_1000[index];
    return (unsigned)(sum % 1000);
}

/* Approximate value as a double using its top two limbs. Relative error about 2^-53. */
static inline double wide_to_double(const struct wide *value, int top_limb) {
    double result = (double)value->limb[top_limb] * limb_scale[top_limb];
    if (top_limb > 0) result += (double)value->limb[top_limb - 1] * limb_scale[top_limb - 1];
    return result;
}

/* Returns 0 only when the number is certainly not a decimal palindrome. */
static inline int decimal_ends_match(const struct wide *value, unsigned residue_1000, int top_limb, int exponent_estimate) {
    if (exponent_estimate < 2) return 1;               /* tiny numbers go straight to the full check */
    double approximation = wide_to_double(value, top_limb);
    int exponent = exponent_estimate;
    double leading = approximation / power10_double[exponent - 2];     /* nominally in [100, 1000) */
    if (leading < 100.0) { exponent--; leading *= 10.0; }
    if (leading >= 1000.0) { exponent++; leading /= 10.0; }
    if (exponent < 2) return 1;
    const double tolerance = 1e-6;
    if (leading < 100.0 + tolerance || leading > 1000.0 - tolerance) return 1;    /* near a digit-count boundary */
    unsigned first_three = (unsigned)leading;        /* leading is positive, so truncation is floor */
    double floor_value = (double)first_three;
    if (leading - floor_value < tolerance || floor_value + 1.0 - leading < tolerance) return 1;   /* near a digit boundary */
    unsigned first = first_three / 100, second = (first_three / 10) % 10, third = first_three % 10;
    return residue_1000 == third * 100 + second * 10 + first;
}

/* Least-significant digit first; returns digit count. */
static int decimal_digits(const struct wide *value, unsigned char *digits) {
    const uint64_t chunk = 10000000000000000000ull;   /* 10^19 */
    struct wide remaining = *value;
    int length = 0;
    if (wide_is_zero(&remaining)) { digits[0] = 0; return 1; }
    while (!wide_is_zero(&remaining)) {
        uint64_t rest = wide_divide_small(&remaining, chunk);
        for (int index = 0; index < 19; index++) {
            digits[length++] = (unsigned char)(rest % 10);
            rest /= 10;
        }
    }
    while (length > 1 && digits[length - 1] == 0) length--;
    return length;
}

static int is_palindrome_base10_wide(const struct wide *value) {
    unsigned char digits[DIGIT_BUFFER];
    int length = decimal_digits(value, digits);
    for (int index = 0; index < length / 2; index++) {
        if (digits[index] != digits[length - 1 - index]) return 0;
    }
    return 1;
}

static void print_hit(const struct wide *value) {
    unsigned char digits[DIGIT_BUFFER];
    int length = decimal_digits(value, digits);
    char decimal[LIMBS * 20 + 2];
    for (int index = 0; index < length; index++) decimal[index] = (char)('0' + digits[length - 1 - index]);
    decimal[length] = 0;

    char hex[MAXIMUM_HEX_DIGITS + 1];
    int hex_length = (wide_bit_length(value) + 3) / 4;
    if (hex_length == 0) hex_length = 1;
    for (int index = 0; index < hex_length; index++) {
        int nibble = hex_length - 1 - index;
        hex[index] = "0123456789ABCDEF"[(value->limb[nibble / 16] >> (4 * (nibble % 16))) & 15];
    }
    hex[hex_length] = 0;

    char binary[MAXIMUM_BITS + 1];
    int bits = wide_bit_length(value);
    for (int index = 0; index < bits; index++) binary[index] = (char)('0' + wide_get_bit(value, bits - 1 - index));
    binary[bits] = 0;

    printf("%s  hex=%s  bin=%s\n", decimal, hex, binary);
    fflush(stdout);
}

/* ---------- orbit groups ---------- */

struct group {
    int hex_length;
    int leading_bits;
    int bit_length;
    int top_limb;
    int exponent_estimate;       /* floor(bit_length * log10(2)), at most one too high */
    int free_count;
    struct wide lead_mask;
    struct wide masks[MAXIMUM_BITS];
    unsigned mask_residue[MAXIMUM_BITS];   /* mask mod 1000 */
    unsigned lead_residue;
    uint64_t count;
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

static int build_group(int hex_length, int leading_bits, struct group *group) {
    int bit_length = 4 * (hex_length - 1) + leading_bits;
    int padded_length = 4 * hex_length;
    int parent[MAXIMUM_BITS];
    for (int position = 0; position < padded_length; position++) parent[position] = position;
    for (int position = 0; position < padded_length; position++) {
        unite(parent, position, 4 * (hex_length - 1 - position / 4) + position % 4);
    }
    for (int position = 0; position < bit_length; position++) unite(parent, position, bit_length - 1 - position);

    unsigned char forced_zero[MAXIMUM_BITS] = {0};
    for (int position = bit_length; position < padded_length; position++) forced_zero[find_root(parent, position)] = 1;
    int lead_root = find_root(parent, bit_length - 1);
    if (forced_zero[lead_root]) return 0;

    memset(group, 0, sizeof *group);
    group->hex_length = hex_length;
    group->leading_bits = leading_bits;
    group->bit_length = bit_length;
    group->top_limb = (bit_length - 1) / 64;
    group->exponent_estimate = (bit_length * 1233) >> 12;
    for (int root = 0; root < bit_length; root++) {
        if (find_root(parent, root) != root || forced_zero[root]) continue;
        struct wide mask = {{0}};
        for (int position = 0; position < bit_length; position++) {
            if (find_root(parent, position) == root) wide_set_bit(&mask, position);
        }
        if (root == lead_root) { group->lead_mask = mask; group->lead_residue = wide_mod_1000(&mask); }
        else { group->mask_residue[group->free_count] = wide_mod_1000(&mask); group->masks[group->free_count++] = mask; }
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
    int verify;                  /* assert incremental residue and prefilter consistency per candidate */
    uint64_t candidates;
    uint64_t found_count;
    uint64_t residue_mismatches;
    uint64_t prefilter_mismatches;
    uint64_t ends_matched;
    uint64_t nine_matched;       /* at least 18 decimal digits with the first and last nine mirrored */
};

static void search(struct worker_arguments *arguments) {
    for (int group_index = 0; group_index < group_count; group_index++) {
        const struct group *group = &groups[group_index];
        uint64_t begin = (uint64_t)((unsigned __int128)group->count * (uint64_t)arguments->thread_index / (uint64_t)arguments->thread_count);
        uint64_t end = (uint64_t)((unsigned __int128)group->count * (uint64_t)(arguments->thread_index + 1) / (uint64_t)arguments->thread_count);
        if (begin >= end) continue;

        uint64_t index = begin;
        uint64_t gray = index ^ (index >> 1);
        struct wide value = group->lead_mask;
        unsigned residue = group->lead_residue;
        for (int bit = 0; bit < group->free_count; bit++) {
            if ((gray >> bit) & 1) { wide_xor(&value, &group->masks[bit]); residue = (residue + group->mask_residue[bit]) % 1000; }
        }
        int top_limb = group->top_limb;
        int exponent_estimate = group->exponent_estimate;

        for (;;) {
            arguments->candidates++;
            if (arguments->verify) {
                if (residue != wide_mod_1000(&value)) arguments->residue_mismatches++;
                unsigned char digits[DIGIT_BUFFER];
                int length = decimal_digits(&value, digits);
                int ends_match = length < 3 || (digits[0] == digits[length - 1] && digits[1] == digits[length - 2] && digits[2] == digits[length - 3]);
                if (ends_match) arguments->ends_matched++;
                int nine_match = length >= 18;
                for (int digit = 0; digit < 9 && nine_match; digit++) if (digits[digit] != digits[length - 1 - digit]) nine_match = 0;
                if (nine_match) arguments->nine_matched++;
                /* the prefilter may never reject a candidate whose first three and last three decimal digits mirror */
                if (ends_match && !decimal_ends_match(&value, residue, top_limb, exponent_estimate)) arguments->prefilter_mismatches++;
                if (is_palindrome_base10_wide(&value)) { arguments->found_count++; print_hit(&value); }
            } else if (arguments->pairs_only) {
                arguments->found_count++;
                print_hit(&value);
            } else if (decimal_ends_match(&value, residue, top_limb, exponent_estimate) && is_palindrome_base10_wide(&value)) {
                arguments->found_count++;
                print_hit(&value);
            }
            index++;
            if (index >= end) break;
            int bit = __builtin_ctzll(index);
            gray ^= 1ull << bit;
            wide_xor(&value, &group->masks[bit]);
            residue += ((gray >> bit) & 1) ? group->mask_residue[bit] : 1000 - group->mask_residue[bit];
            if (residue >= 1000) residue -= 1000;
        }
        if (arguments->thread_index == 0 && !arguments->pairs_only && !arguments->verify) {
            fprintf(stderr, "  finished hex_length=%d leading_bits=%d (%llu candidates)\n",
                    group->hex_length, group->leading_bits, (unsigned long long)group->count);
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

/* ---------- self test ---------- */

static void wide_from_decimal(struct wide *value, const char *text) {
    memset(value, 0, sizeof *value);
    for (; *text; text++) wide_multiply_add(value, 10, (uint64_t)(*text - '0'));
}

static int string_is_palindrome(const char *text) {
    size_t length = strlen(text);
    for (size_t index = 0; index < length / 2; index++) if (text[index] != text[length - 1 - index]) return 0;
    return 1;
}

static int is_palindrome_base10_64(uint64_t value) {
    uint64_t reversed = 0, remaining = value;
    while (remaining != 0) { reversed = reversed * 10 + remaining % 10; remaining /= 10; }
    return reversed == value;
}

static unsigned pass_reason[3];   /* 0: short number, 1: residue matched reversed leading digits, 2: tolerance fall-through */

static int check_value(const struct wide *value, const char *text, int *failures) {
    int expected = string_is_palindrome(text);
    int top_limb = 0;
    int bit_length = wide_bit_length(value);
    if (bit_length > 0) top_limb = (bit_length - 1) / 64;
    int exponent_estimate = (bit_length * 1233) >> 12;
    int full = is_palindrome_base10_wide(value);
    int prefilter = decimal_ends_match(value, wide_mod_1000(value), top_limb, exponent_estimate);
    if (full != expected) { (*failures)++; printf("full check wrong for %s\n", text); }
    if (expected && !prefilter) { (*failures)++; printf("prefilter rejected palindrome %s\n", text); }
    if (!expected && prefilter) {
        size_t length = strlen(text);
        if (length < 3) pass_reason[0]++;
        else {
            unsigned leading = (unsigned)((text[0] - '0') * 100 + (text[1] - '0') * 10 + (text[2] - '0'));
            unsigned trailing = (unsigned)((text[length - 1] - '0') * 100 + (text[length - 2] - '0') * 10 + (text[length - 3] - '0'));
            pass_reason[leading == trailing ? 1 : 2]++;
        }
    }
    return prefilter;
}

static int selftest(void) {
    int failures = 0;
    char text[LIMBS * 20 + 2];

    for (uint64_t number = 1; number < (1ull << 20); number++) {
        struct wide value = {{number, 0, 0, 0}};
        int expected = is_palindrome_base10_64(number);
        snprintf(text, sizeof text, "%llu", (unsigned long long)number);
        check_value(&value, text, &failures);
        if (is_palindrome_base10_wide(&value) != expected) { failures++; printf("64-bit disagreement at %llu\n", (unsigned long long)number); }
    }

    /* random decimal strings of every length, half of them forced palindromic */
    srand(12345);
    unsigned prefilter_passes = 0, prefilter_total = 0;
    for (int length = 1; length <= 77; length++) {
        for (int trial = 0; trial < 400; trial++) {
            for (int index = 0; index < length; index++) text[index] = (char)('0' + rand() % 10);
            if (text[0] == '0') text[0] = '1';
            if (trial % 2 == 0) for (int index = 0; index < length / 2; index++) text[length - 1 - index] = text[index];
            text[length] = 0;
            struct wide value;
            wide_from_decimal(&value, text);
            prefilter_total++;
            prefilter_passes += (unsigned)check_value(&value, text, &failures);
        }
    }
    printf("selftest: %d failures; prefilter passed %u of %u random values (half are palindromes)\n",
           failures, prefilter_passes, prefilter_total);
    printf("  non-palindrome passes: %u short numbers, %u genuine leading/trailing matches, %u tolerance fall-throughs\n",
           pass_reason[0], pass_reason[1], pass_reason[2]);
    return failures == 0 ? 0 : 1;
}

int main(int argc, char **argv) {
    initialize_tables();
    if (argc > 1 && strcmp(argv[1], "selftest") == 0) return selftest();

    int pairs_only = 0, verify = 0;
    int argument_index = 1;
    if (argc > 1 && strcmp(argv[1], "pairs") == 0) { pairs_only = 1; argument_index = 2; }
    if (argc > 1 && strcmp(argv[1], "verify") == 0) { verify = 1; argument_index = 2; }
    if (argc < argument_index + 2) {
        fprintf(stderr, "usage: tripal_wide [pairs] <minimum_hex_digits> <maximum_hex_digits> [threads] | selftest\n");
        return 1;
    }
    int minimum_length = atoi(argv[argument_index]);
    int maximum_length = atoi(argv[argument_index + 1]);
    int thread_count = (argc > argument_index + 2) ? atoi(argv[argument_index + 2]) : hardware_thread_count();
    const char *leading_filter = (argc > argument_index + 3) ? argv[argument_index + 3] : "1234";
    if (minimum_length < 1 || maximum_length > MAXIMUM_HEX_DIGITS || minimum_length > maximum_length) {
        fprintf(stderr, "hex digit range must satisfy 1 <= minimum <= maximum <= %d\n", MAXIMUM_HEX_DIGITS);
        return 1;
    }
    if (thread_count < 1) thread_count = 1;

    uint64_t total_candidates = 0;
    for (int hex_length = minimum_length; hex_length <= maximum_length; hex_length++) {
        for (int leading_bits = 1; leading_bits <= 4; leading_bits++) {
            if (strchr(leading_filter, '0' + leading_bits) == NULL) continue;
            if (build_group(hex_length, leading_bits, &groups[group_count])) {
                total_candidates += groups[group_count].count;
                group_count++;
            }
        }
    }
    printf("searching base 2 + base 16 palindromes with %d..%d hex digits, leading bits {%s}: %d groups, %llu candidates, %d threads%s\n",
           minimum_length, maximum_length, leading_filter, group_count, (unsigned long long)total_candidates, thread_count,
           pairs_only ? " (pairs mode)" : "");
    fflush(stdout);

    struct worker_arguments *arguments = calloc((size_t)thread_count, sizeof *arguments);
    thread_handle *threads = calloc((size_t)thread_count, sizeof *threads);
    clock_t wall_start = clock();
    for (int index = 0; index < thread_count; index++) {
        arguments[index].thread_index = index;
        arguments[index].thread_count = thread_count;
        arguments[index].pairs_only = pairs_only;
        arguments[index].verify = verify;
#ifdef _WIN32
        threads[index] = CreateThread(NULL, 0, worker_entry, &arguments[index], 0, NULL);
#else
        pthread_create(&threads[index], NULL, worker_entry, &arguments[index]);
#endif
    }
    uint64_t total_found = 0, visited = 0, residue_mismatches = 0, prefilter_mismatches = 0, ends_matched = 0, nine_matched = 0;
    for (int index = 0; index < thread_count; index++) {
#ifdef _WIN32
        WaitForSingleObject(threads[index], INFINITE);
        CloseHandle(threads[index]);
#else
        pthread_join(threads[index], NULL);
#endif
        total_found += arguments[index].found_count;
        visited += arguments[index].candidates;
        residue_mismatches += arguments[index].residue_mismatches;
        prefilter_mismatches += arguments[index].prefilter_mismatches;
        ends_matched += arguments[index].ends_matched;
        nine_matched += arguments[index].nine_matched;
    }
    if (verify) printf("verify: %llu residue mismatches, %llu prefilter rejections among %llu candidates with mirrored end digits, %llu candidates with nine mirrored end digits\n",
                       (unsigned long long)residue_mismatches, (unsigned long long)prefilter_mismatches, (unsigned long long)ends_matched, (unsigned long long)nine_matched);
    double elapsed = (double)(clock() - wall_start) / CLOCKS_PER_SEC;
    printf("done: %llu hits among %llu candidates visited in %.1f s\n",
           (unsigned long long)total_found, (unsigned long long)visited, elapsed);
    free(arguments);
    free(threads);
    return 0;
}
