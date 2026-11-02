/* tripal_cuda: GPU search for integers palindromic in bases 2, 10 and 16, up to 256 bits.
 *
 * Same orbit construction as tripal_wide.c: for each (hex_length, leading_bits)
 * group the host computes the free orbit masks; the device enumerates orbit
 * assignments by Gray code, one contiguous index slice per thread, flipping one
 * mask per step and tracking the value mod 10^9 incrementally.
 *
 * Two prefilter stages run on the device, both of which may only err by passing:
 *   stage 1: the top 32 bits give the first three decimal digits as a float;
 *            they must mirror the last three (residue mod 1000).
 *   stage 2: the top 64 bits give the first nine decimal digits as a double;
 *            they must mirror the last nine (residue mod 10^9).
 * Anything near a rounding boundary, any number under 18 decimal digits, and any
 * group under 64 bits passes to the host, which runs the exact decimal check.
 *
 * Build:  build_cuda.bat on Windows (finds MSVC via vswhere), or
 *         nvcc -O3 -arch=native -o bin/tripal_cuda src/tripal_cuda.cu
 * Run:    tripal_cuda <minimum_hex_digits> <maximum_hex_digits> [leading_bits]
 *         tripal_cuda stage1 <minimum> <maximum> [leading_bits]   stage-1 survivors only, classified on host
 *         tripal_cuda all <minimum> <maximum> [leading_bits]      every candidate, printed like tripal_wide pairs
 */
#include <cuda_runtime.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <chrono>
#include <utility>
#include <vector>
#ifdef _MSC_VER
#include <intrin.h>
#endif

#define LIMBS 4
#define MAXIMUM_BITS (64 * LIMBS)
#define MAXIMUM_HEX_DIGITS (MAXIMUM_BITS / 4)
#define MAXIMUM_ORBITS 128
#define MAXIMUM_DECIMAL_DIGITS 78
#define DIGIT_BUFFER (19 * (MAXIMUM_DECIMAL_DIGITS / 19 + 1))
#define RESIDUE_MODULUS 1000000000u
#define SURVIVOR_CAPACITY (1u << 24)          /* 16M entries, 512 MB of device memory */
#define CHUNK_SIZE (1ull << 38)                /* indices per kernel launch */

enum search_mode { MODE_SEARCH = 0, MODE_STAGE1 = 1, MODE_ALL = 2 };

struct wide { uint64_t limb[LIMBS]; };

struct group_description {
    int hex_length;
    int leading_bits;
    int bit_length;
    int free_count;
    int exponent_estimate;       /* floor(bit_length * log10 2), at most one too high */
    float stage1_scale;          /* 2^(bit_length-32) / 10^(exponent_estimate-2) */
    double stage2_scale;         /* 2^(bit_length-64) / 10^(exponent_estimate-8) */
    uint32_t lead_residue;
    uint64_t count;
    wide lead_mask;
    wide masks[MAXIMUM_ORBITS];
    uint32_t mask_residue[MAXIMUM_ORBITS];
};

/* ---------- device ---------- */

__constant__ group_description device_group;

__device__ __forceinline__ uint64_t extract_bits(const wide &value, int low_bit) {
    int limb_index = low_bit >> 6;
    int offset = low_bit & 63;
    uint64_t result = value.limb[limb_index] >> offset;
    if (offset != 0 && limb_index + 1 < LIMBS) result |= value.limb[limb_index + 1] << (64 - offset);
    return result;
}

__global__ void search_kernel(int mode, uint64_t chunk_begin, uint64_t chunk_end,
                              wide *survivors, unsigned *survivor_count, unsigned capacity) {
    __shared__ wide masks[MAXIMUM_ORBITS];
    __shared__ uint32_t mask_residue[MAXIMUM_ORBITS];
    const int free_count = device_group.free_count;
    for (int index = threadIdx.x; index < free_count; index += blockDim.x) {
        masks[index] = device_group.masks[index];
        mask_residue[index] = device_group.mask_residue[index];
    }
    __syncthreads();

    const uint64_t total = chunk_end - chunk_begin;
    const uint64_t thread_count = (uint64_t)gridDim.x * blockDim.x;
    const uint64_t thread_id = (uint64_t)blockIdx.x * blockDim.x + threadIdx.x;
    const uint64_t base = total / thread_count;
    const uint64_t remainder = total % thread_count;
    uint64_t begin = chunk_begin + base * thread_id + (thread_id < remainder ? thread_id : remainder);
    const uint64_t end = begin + base + (thread_id < remainder ? 1 : 0);
    if (begin >= end) return;

    const int bit_length = device_group.bit_length;
    const int exponent_estimate = device_group.exponent_estimate;
    const float stage1_scale = device_group.stage1_scale;
    const double stage2_scale = device_group.stage2_scale;
    const bool small_group = bit_length < 64;

    uint64_t index = begin;
    uint64_t gray = index ^ (index >> 1);
    wide value = device_group.lead_mask;
    uint32_t residue = device_group.lead_residue;
    for (int bit = 0; bit < free_count; bit++) {
        if ((gray >> bit) & 1) {
            for (int limb = 0; limb < LIMBS; limb++) value.limb[limb] ^= masks[bit].limb[limb];
            residue += mask_residue[bit];
            if (residue >= RESIDUE_MODULUS) residue -= RESIDUE_MODULUS;
        }
    }

    for (;;) {
        bool survive;
        if (mode == MODE_ALL || small_group) {
            survive = true;
        } else {
            /* stage 1: three leading digits from the top 32 bits */
            uint32_t top32 = (uint32_t)extract_bits(value, bit_length - 32);
            float leading = (float)top32 * stage1_scale;
            if (leading < 100.0f) leading *= 10.0f;
            else if (leading >= 1000.0f) leading *= 0.1f;
            const float tolerance = 1e-3f;
            if (leading < 100.0f + tolerance || leading > 1000.0f - tolerance) {
                survive = true;
            } else {
                unsigned first_three = (unsigned)leading;
                float fraction = leading - (float)first_three;
                if (fraction < tolerance || fraction > 1.0f - tolerance) {
                    survive = true;
                } else {
                    unsigned reversed = (first_three % 10) * 100 + ((first_three / 10) % 10) * 10 + first_three / 100;
                    survive = (residue % 1000u) == reversed;
                }
            }
            if (survive && mode == MODE_SEARCH) {
                /* stage 2: nine leading digits from the top 64 bits */
                uint64_t top64 = extract_bits(value, bit_length - 64);
                double leading9 = (double)top64 * stage2_scale;
                int exponent = exponent_estimate;
                if (leading9 < 1e8) { leading9 *= 10.0; exponent--; }
                else if (leading9 >= 1e9) { leading9 *= 0.1; exponent++; }
                const double tolerance9 = 1e-4;
                if (exponent + 1 < 18) {
                    survive = true;          /* leading and trailing nine digits would overlap */
                } else if (leading9 < 1e8 + tolerance9 || leading9 > 1e9 - tolerance9) {
                    survive = true;
                } else {
                    uint32_t first_nine = (uint32_t)leading9;
                    double fraction = leading9 - (double)first_nine;
                    if (fraction < tolerance9 || fraction > 1.0 - tolerance9) {
                        survive = true;
                    } else {
                        uint32_t reversed = 0, remaining = first_nine;
                        for (int digit = 0; digit < 9; digit++) { reversed = reversed * 10 + remaining % 10; remaining /= 10; }
                        survive = residue == reversed;
                    }
                }
            }
        }
        if (survive) {
            unsigned slot = atomicAdd(survivor_count, 1u);
            if (slot < capacity) survivors[slot] = value;
        }
        index++;
        if (index >= end) break;
        int bit = __ffsll((long long)index) - 1;
        gray ^= 1ull << bit;
        for (int limb = 0; limb < LIMBS; limb++) value.limb[limb] ^= masks[bit].limb[limb];
        residue += ((gray >> bit) & 1) ? mask_residue[bit] : RESIDUE_MODULUS - mask_residue[bit];
        if (residue >= RESIDUE_MODULUS) residue -= RESIDUE_MODULUS;
    }
}

/* ---------- host: wide arithmetic ---------- */

static uint64_t divide_128_by_64(uint64_t high, uint64_t low, uint64_t divisor, uint64_t *remainder) {
#ifdef _MSC_VER
    return _udiv128(high, low, divisor, remainder);
#else
    unsigned __int128 current = ((unsigned __int128)high << 64) | low;
    *remainder = (uint64_t)(current % divisor);
    return (uint64_t)(current / divisor);
#endif
}

static uint64_t wide_divide_small(wide *value, uint64_t divisor) {
    uint64_t remainder = 0;
    for (int index = LIMBS - 1; index >= 0; index--) {
        value->limb[index] = divide_128_by_64(remainder, value->limb[index], divisor, &remainder);
    }
    return remainder;
}

static uint32_t wide_mod_residue(const wide &value) {
    wide copy = value;
    return (uint32_t)wide_divide_small(&copy, RESIDUE_MODULUS);
}

static int wide_is_zero(const wide &value) {
    uint64_t any = 0;
    for (int index = 0; index < LIMBS; index++) any |= value.limb[index];
    return any == 0;
}

static int wide_bit_length(const wide &value) {
    for (int index = LIMBS - 1; index >= 0; index--) {
        if (value.limb[index] != 0) {
            int leading = 0;
            uint64_t limb = value.limb[index];
            while (!(limb & (1ull << 63))) { limb <<= 1; leading++; }
            return 64 * index + 64 - leading;
        }
    }
    return 0;
}

static void wide_set_bit(wide *value, int position) { value->limb[position / 64] |= 1ull << (position % 64); }
static int wide_get_bit(const wide &value, int position) { return (int)((value.limb[position / 64] >> (position % 64)) & 1); }

static int decimal_digits(const wide &value, unsigned char *digits) {
    const uint64_t chunk = 10000000000000000000ull;   /* 10^19 */
    wide remaining = value;
    int length = 0;
    if (wide_is_zero(remaining)) { digits[0] = 0; return 1; }
    while (!wide_is_zero(remaining)) {
        uint64_t rest = wide_divide_small(&remaining, chunk);
        for (int index = 0; index < 19; index++) { digits[length++] = (unsigned char)(rest % 10); rest /= 10; }
    }
    while (length > 1 && digits[length - 1] == 0) length--;
    return length;
}

static int is_palindrome_base10(const wide &value) {
    unsigned char digits[DIGIT_BUFFER];
    int length = decimal_digits(value, digits);
    for (int index = 0; index < length / 2; index++) if (digits[index] != digits[length - 1 - index]) return 0;
    return 1;
}

/* 1 if the first and last `width` decimal digits mirror; requires at least 2*width digits */
static int ends_mirror(const wide &value, int width) {
    unsigned char digits[DIGIT_BUFFER];
    int length = decimal_digits(value, digits);
    if (length < 2 * width) return -1;
    for (int index = 0; index < width; index++) if (digits[index] != digits[length - 1 - index]) return 0;
    return 1;
}

static void print_value(const wide &value) {
    unsigned char digits[DIGIT_BUFFER];
    int length = decimal_digits(value, digits);
    char decimal[DIGIT_BUFFER + 1];
    for (int index = 0; index < length; index++) decimal[index] = (char)('0' + digits[length - 1 - index]);
    decimal[length] = 0;

    char hex[MAXIMUM_HEX_DIGITS + 1];
    int hex_length = (wide_bit_length(value) + 3) / 4;
    if (hex_length == 0) hex_length = 1;
    for (int index = 0; index < hex_length; index++) {
        int nibble = hex_length - 1 - index;
        hex[index] = "0123456789ABCDEF"[(value.limb[nibble / 16] >> (4 * (nibble % 16))) & 15];
    }
    hex[hex_length] = 0;

    char binary[MAXIMUM_BITS + 1];
    int bits = wide_bit_length(value);
    for (int index = 0; index < bits; index++) binary[index] = (char)('0' + wide_get_bit(value, bits - 1 - index));
    binary[bits] = 0;
    printf("%s  hex=%s  bin=%s\n", decimal, hex, binary);
}

/* ---------- host: orbit groups ---------- */

static int find_root(int *parent, int position) {
    while (parent[position] != position) { parent[position] = parent[parent[position]]; position = parent[position]; }
    return position;
}

static void unite(int *parent, int first, int second) {
    first = find_root(parent, first);
    second = find_root(parent, second);
    if (first != second) parent[first] = second;
}

static int build_group(int hex_length, int leading_bits, group_description *group) {
    int bit_length = 4 * (hex_length - 1) + leading_bits;
    int padded_length = 4 * hex_length;
    int parent[MAXIMUM_BITS];
    for (int position = 0; position < padded_length; position++) parent[position] = position;
    for (int position = 0; position < padded_length; position++) unite(parent, position, 4 * (hex_length - 1 - position / 4) + position % 4);
    for (int position = 0; position < bit_length; position++) unite(parent, position, bit_length - 1 - position);

    unsigned char forced_zero[MAXIMUM_BITS] = {0};
    for (int position = bit_length; position < padded_length; position++) forced_zero[find_root(parent, position)] = 1;
    int lead_root = find_root(parent, bit_length - 1);
    if (forced_zero[lead_root]) return 0;

    memset(group, 0, sizeof *group);
    group->hex_length = hex_length;
    group->leading_bits = leading_bits;
    group->bit_length = bit_length;
    group->exponent_estimate = (bit_length * 1233) >> 12;
    for (int root = 0; root < bit_length; root++) {
        if (find_root(parent, root) != root || forced_zero[root]) continue;
        wide mask = {{0, 0, 0, 0}};
        for (int position = 0; position < bit_length; position++) if (find_root(parent, position) == root) wide_set_bit(&mask, position);
        if (root == lead_root) { group->lead_mask = mask; group->lead_residue = wide_mod_residue(mask); }
        else {
            if (group->free_count >= MAXIMUM_ORBITS) { fprintf(stderr, "too many orbits\n"); exit(1); }
            group->mask_residue[group->free_count] = wide_mod_residue(mask);
            group->masks[group->free_count++] = mask;
        }
    }
    group->count = 1ull << group->free_count;
    if (bit_length >= 64) {
        group->stage1_scale = (float)(ldexp(1.0, bit_length - 32) / pow(10.0, group->exponent_estimate - 2));
        group->stage2_scale = ldexp(1.0, bit_length - 64) / pow(10.0, group->exponent_estimate - 8);
    }
    return 1;
}

/* ---------- host: driver ---------- */

#define CUDA_CHECK(call) do { cudaError_t error = (call); if (error != cudaSuccess) { \
    fprintf(stderr, "CUDA error %s at %s:%d\n", cudaGetErrorString(error), __FILE__, __LINE__); exit(1); } } while (0)

int main(int argc, char **argv) {
    int mode = MODE_SEARCH;
    int argument_index = 1;
    if (argc > 1 && strcmp(argv[1], "stage1") == 0) { mode = MODE_STAGE1; argument_index = 2; }
    if (argc > 1 && strcmp(argv[1], "all") == 0) { mode = MODE_ALL; argument_index = 2; }
    if (argc < argument_index + 2) {
        fprintf(stderr, "usage: tripal_cuda [stage1|all] <minimum_hex_digits> <maximum_hex_digits> [leading_bits]\n");
        return 1;
    }
    int minimum_length = atoi(argv[argument_index]);
    int maximum_length = atoi(argv[argument_index + 1]);
    const char *leading_filter = (argc > argument_index + 2) ? argv[argument_index + 2] : "1234";
    if (minimum_length < 1 || maximum_length > MAXIMUM_HEX_DIGITS || minimum_length > maximum_length) {
        fprintf(stderr, "hex digit range must satisfy 1 <= minimum <= maximum <= %d\n", MAXIMUM_HEX_DIGITS);
        return 1;
    }

    std::vector<group_description> groups;
    uint64_t total_candidates = 0;
    for (int hex_length = minimum_length; hex_length <= maximum_length; hex_length++) {
        for (int leading_bits = 1; leading_bits <= 4; leading_bits++) {
            if (strchr(leading_filter, '0' + leading_bits) == NULL) continue;
            group_description group;
            if (build_group(hex_length, leading_bits, &group)) { total_candidates += group.count; groups.push_back(group); }
        }
    }

    int device = 0;
    cudaDeviceProp properties;
    CUDA_CHECK(cudaGetDeviceProperties(&properties, device));
    const int block_size = 256;
    const int grid_size = properties.multiProcessorCount * 32;
    printf("searching base 2 + base 16 palindromes with %d..%d hex digits, leading bits {%s}: %zu groups, %llu candidates, %s, %d blocks x %d threads%s\n",
           minimum_length, maximum_length, leading_filter, groups.size(), (unsigned long long)total_candidates,
           properties.name, grid_size, block_size, mode == MODE_STAGE1 ? " (stage1 mode)" : mode == MODE_ALL ? " (all mode)" : "");
    fflush(stdout);

    wide *device_survivors;
    unsigned *device_count;
    CUDA_CHECK(cudaMalloc(&device_survivors, sizeof(wide) * (size_t)SURVIVOR_CAPACITY));
    CUDA_CHECK(cudaMalloc(&device_count, sizeof(unsigned)));
    std::vector<wide> survivors;

    uint64_t hits = 0, visited = 0, survivor_total = 0;
    uint64_t short_numbers = 0, mirrored = 0, fall_through = 0;
    int debug_examples = getenv("TRIPAL_DEBUG") ? 8 : 0;   /* print a few fall-through survivors to stderr */
    auto wall_start = std::chrono::steady_clock::now();
    for (size_t group_index = 0; group_index < groups.size(); group_index++) {
        const group_description &group = groups[group_index];
        CUDA_CHECK(cudaMemcpyToSymbol(device_group, &group, sizeof group));
        unsigned group_survivors = 0;
        /* Large groups run in index chunks so the survivor buffer stays bounded. Survivor
         * counts are quantized by shared leading-bit prefixes, so a chunk can overflow
         * unexpectedly; an overflowing chunk is split in half and both halves retried. */
        std::vector<std::pair<uint64_t, uint64_t>> pending;
        for (uint64_t chunk_begin = 0; chunk_begin < group.count; chunk_begin += CHUNK_SIZE) {
            uint64_t chunk_end = chunk_begin + CHUNK_SIZE < group.count ? chunk_begin + CHUNK_SIZE : group.count;
            pending.push_back(std::make_pair(chunk_begin, chunk_end));
        }
        while (!pending.empty()) {
            uint64_t chunk_begin = pending.back().first, chunk_end = pending.back().second;
            pending.pop_back();
            CUDA_CHECK(cudaMemset(device_count, 0, sizeof(unsigned)));
            search_kernel<<<grid_size, block_size>>>(mode, chunk_begin, chunk_end, device_survivors, device_count, SURVIVOR_CAPACITY);
            CUDA_CHECK(cudaGetLastError());
            CUDA_CHECK(cudaDeviceSynchronize());
            unsigned count;
            CUDA_CHECK(cudaMemcpy(&count, device_count, sizeof count, cudaMemcpyDeviceToHost));
            if (count > SURVIVOR_CAPACITY) {
                if (chunk_end - chunk_begin < 2) {
                    fprintf(stderr, "survivor buffer overflow on a single index in group hex_length=%d leading_bits=%d\n",
                            group.hex_length, group.leading_bits);
                    return 1;
                }
                uint64_t middle = chunk_begin + (chunk_end - chunk_begin) / 2;
                fprintf(stderr, "  splitting chunk [%llu, %llu) of hex_length=%d leading_bits=%d: %u survivors exceed capacity %u\n",
                        (unsigned long long)chunk_begin, (unsigned long long)chunk_end, group.hex_length, group.leading_bits, count, SURVIVOR_CAPACITY);
                pending.push_back(std::make_pair(middle, chunk_end));
                pending.push_back(std::make_pair(chunk_begin, middle));
                continue;
            }
            survivors.resize(count);
            if (count > 0) CUDA_CHECK(cudaMemcpy(survivors.data(), device_survivors, sizeof(wide) * count, cudaMemcpyDeviceToHost));
            visited += chunk_end - chunk_begin;
            survivor_total += count;
            group_survivors += count;

            for (unsigned index = 0; index < count; index++) {
                const wide &value = survivors[index];
                if (mode == MODE_ALL) { print_value(value); continue; }
                int width = (mode == MODE_STAGE1) ? 3 : 9;
                int mirror = ends_mirror(value, width);
                if (mirror < 0) short_numbers++;
                else if (mirror == 1) mirrored++;
                else {
                    fall_through++;
                    if (debug_examples > 0) { debug_examples--; fprintf(stderr, "  fall-through example: "); print_value(value); }
                }
                if (mode == MODE_SEARCH && is_palindrome_base10(value)) { hits++; print_value(value); fflush(stdout); }
            }
        }
        if (mode == MODE_SEARCH) {
            double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall_start).count();
            fprintf(stderr, "  finished hex_length=%d leading_bits=%d (%llu candidates, %u survivors, %.1f s elapsed)\n",
                    group.hex_length, group.leading_bits, (unsigned long long)group.count, group_survivors, elapsed);
        }
    }
    double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall_start).count();
    if (mode != MODE_ALL) {
        printf("survivors: %llu total; %llu under %d decimal digits, %llu with genuinely mirrored %d-digit ends, %llu rounding fall-throughs\n",
               (unsigned long long)survivor_total, (unsigned long long)short_numbers, mode == MODE_STAGE1 ? 6 : 18,
               (unsigned long long)mirrored, mode == MODE_STAGE1 ? 3 : 9, (unsigned long long)fall_through);
    }
    printf("done: %llu hits among %llu candidates visited in %.1f s\n", (unsigned long long)hits, (unsigned long long)visited, elapsed);
    CUDA_CHECK(cudaFree(device_survivors));
    CUDA_CHECK(cudaFree(device_count));
    return 0;
}
