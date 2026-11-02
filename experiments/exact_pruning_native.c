/* Exact outside-in experiment, deliberately independent of production search. */
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct wide { uint64_t limb[4]; };
struct hit { char digits[79]; };
struct search {
    int length, leading_bits, independent_digits, digit_count;
    const unsigned *digits;
    struct wide remaining[33];
    uint64_t subtree_counts[33];
    uint64_t assignments, visited_nodes, pruned_branches;
    uint64_t pruned_assignments, examined_leaves;
    struct hit *hits;
    size_t hit_count, hit_capacity;
};

static const unsigned endpoints[4][2] = {{1, 0}, {3, 0}, {5, 7}, {9, 15}};
static const unsigned inner_digits[4][4] = {{0, 1, 0, 0}, {0, 3, 0, 0},
                                          {0, 2, 5, 7}, {0, 6, 9, 15}};

/* high < divisor, so the quotient fits in one limb. */
static uint64_t divide_limb(uint64_t high, uint64_t low, uint64_t *remainder) {
    const uint64_t divisor = UINT64_C(10000000000000000000);
#if defined(__x86_64__)
    uint64_t quotient;
    __asm__("divq %4" : "=a"(quotient), "=d"(*remainder)
            : "a"(low), "d"(high), "r"(divisor));
    return quotient;
#else
    unsigned __int128 current = ((unsigned __int128)high << 64) | low;
    *remainder = (uint64_t)(current % divisor);
    return (uint64_t)(current / divisor);
#endif
}

static void decimal_digits(struct wide value, char output[79]) {
    uint64_t chunks[5];
    int count = 0;
    do {
        uint64_t remainder = 0;
        for (int index = 3; index >= 0; --index)
            value.limb[index] = divide_limb(remainder, value.limb[index], &remainder);
        chunks[count++] = remainder;
    } while (value.limb[0] | value.limb[1] | value.limb[2] | value.limb[3]);
    int position = sprintf(output, "%" PRIu64, chunks[--count]);
    while (count > 0)
        position += sprintf(output + position, "%019" PRIu64, chunks[--count]);
}

static struct wide assign_digit(struct wide value, int length, int depth, unsigned digit) {
    int low_position = 4 * depth;
    int high_position = 4 * (length - 1 - depth);
    value.limb[low_position / 64] |= (uint64_t)digit << (low_position % 64);
    /* OR visits an odd center once, even when both positions coincide. */
    value.limb[high_position / 64] |= (uint64_t)digit << (high_position % 64);
    return value;
}

static int interval_rejects(struct wide lower, struct wide remaining, int fixed_bits) {
    struct wide upper = lower;
    for (int index = 0; index < 4; ++index) upper.limb[index] |= remaining.limb[index];
    char lower_digits[79], upper_digits[79];
    decimal_digits(lower, lower_digits);
    decimal_digits(upper, upper_digits);
    size_t decimal_length = strlen(lower_digits);
    if (decimal_length != strlen(upper_digits)) return 0;
    int maximum_digits = (int)(decimal_length / 2);
    if (maximum_digits > fixed_bits) maximum_digits = fixed_bits;
    int common_digits = 0;
    uint64_t required_suffix = 0, decimal_power = 1;
    /* At most 39 digits. Reduce every term modulo 2^maximum_digits. */
    uint64_t mask = (UINT64_C(1) << maximum_digits) - 1;
    while (common_digits < maximum_digits &&
           lower_digits[common_digits] == upper_digits[common_digits]) {
        required_suffix = (required_suffix +
                           (unsigned)(lower_digits[common_digits] - '0') * decimal_power) & mask;
        decimal_power = (decimal_power * 10) & mask;
        ++common_digits;
    }
    mask = (UINT64_C(1) << common_digits) - 1;
    return (required_suffix & mask) != (lower.limb[0] & mask);
}

static void examine_leaf(struct search *search, struct wide value) {
    char digits[79];
    decimal_digits(value, digits);
    size_t length = strlen(digits);
    for (size_t index = 0; index < length / 2; ++index)
        if (digits[index] != digits[length - 1 - index]) return;
    if (search->hit_count == search->hit_capacity) {
        size_t capacity = search->hit_capacity ? search->hit_capacity * 2 : 8;
        struct hit *hits = realloc(search->hits, capacity * sizeof(*hits));
        if (!hits) { fputs("out of memory\n", stderr); exit(1); }
        search->hits = hits;
        search->hit_capacity = capacity;
    }
    memcpy(search->hits[search->hit_count++].digits, digits, length + 1);
}

static void visit(struct search *search, int depth, struct wide value) {
    ++search->visited_nodes;
    if (depth == search->independent_digits) {
        ++search->examined_leaves;
        examine_leaf(search, value);
        return;
    }
    if (interval_rejects(value, search->remaining[depth], 4 * depth)) {
        ++search->pruned_branches;
        search->pruned_assignments += search->subtree_counts[depth];
        return;
    }
    for (int index = 0; index < search->digit_count; ++index)
        visit(search, depth + 1,
              assign_digit(value, search->length, depth, search->digits[index]));
}

static void run_group(int length, int leading_bits) {
    struct search search = {0};
    search.length = length;
    search.leading_bits = leading_bits;
    search.independent_digits = (length + 1) / 2;
    search.digit_count = leading_bits <= 2 ? 2 : 4;
    search.digits = inner_digits[leading_bits - 1];
    search.subtree_counts[search.independent_digits] = 1;
    /* Depth zero would overflow at length 64; it is never used. */
    for (int depth = search.independent_digits - 1; depth >= 1; --depth) {
        search.remaining[depth] = assign_digit(search.remaining[depth + 1], length,
                                    depth, search.digits[search.digit_count - 1]);
        search.subtree_counts[depth] = search.subtree_counts[depth + 1] * search.digit_count;
    }
    int endpoint_count = leading_bits <= 2 ? 1 : 2;
    search.assignments = endpoint_count * search.subtree_counts[1];
    for (int index = 0; index < endpoint_count; ++index) {
        struct wide zero = {{0, 0, 0, 0}};
        visit(&search, 1, assign_digit(zero, length, 0, endpoints[leading_bits - 1][index]));
    }
    uint64_t covered = search.pruned_assignments + search.examined_leaves;
    if (covered != search.assignments) {
        fputs("assignment accounting failure\n", stderr);
        exit(1);
    }
    printf("{\"length\":%d,\"leading_bits\":%d,\"assignments\":%" PRIu64
           ",\"visited_nodes\":%" PRIu64 ",\"pruned_branches\":%" PRIu64
           ",\"pruned_assignments\":%" PRIu64 ",\"examined_leaves\":%" PRIu64
           ",\"covered_assignments\":%" PRIu64 ",\"completed\":true,\"hits\":[",
           length, leading_bits, search.assignments, search.visited_nodes,
           search.pruned_branches, search.pruned_assignments, search.examined_leaves, covered);
    /* Ascending endpoints and outside-in digits produce numeric sorted order. */
    for (size_t index = 0; index < search.hit_count; ++index)
        printf("%s%s", index ? "," : "", search.hits[index].digits);
    puts("]}");
    fflush(stdout);
    free(search.hits);
}

static int parse_number(const char *text, int maximum) {
    char *end;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (errno || end == text || *end || value < 1 || value > maximum) return 0;
    return (int)value;
}

int main(int argument_count, char **arguments) {
    int selected[4] = {1, 1, 1, 1};
    if (argument_count < 3) goto invalid;
    int minimum_length = parse_number(arguments[1], 64);
    int maximum_length = parse_number(arguments[2], 64);
    if (!minimum_length || maximum_length < minimum_length) goto invalid;
    if (argument_count > 3) {
        if (argument_count < 5 || strcmp(arguments[3], "--leading-bits")) goto invalid;
        memset(selected, 0, sizeof(selected));
        for (int index = 4; index < argument_count; ++index) {
            int leading_bits = parse_number(arguments[index], 4);
            if (!leading_bits) goto invalid;
            selected[leading_bits - 1] = 1;
        }
    }
    for (int length = minimum_length; length <= maximum_length; ++length)
        for (int leading_bits = 1; leading_bits <= 4; ++leading_bits)
            if (selected[leading_bits - 1]) run_group(length, leading_bits);
    return 0;
invalid:
    fputs("usage: exact_pruning_native minimum maximum [--leading-bits 1 2 3 4]\n"
          "require 1 <= minimum <= maximum <= 64 and leading bits 1 through 4\n", stderr);
    return 2;
}
