/* Include the production kernel and driver helpers without a second entry point. */
#define main tripal_cuda_program_main
#include "../src/tripal_cuda.cu"
#undef main
#include <algorithm>

template <typename Signature> struct kernel_counter;
template <typename Counter>
struct kernel_counter<void (*)(int, uint64_t, uint64_t, wide *, Counter *, unsigned)> {
    using type = Counter;
};
using counter_type = kernel_counter<decltype(&search_kernel)>::type;

static bool counter_crosses_32_bits() {
    group_description group;
    if (!build_group(5, 3, &group) || group.count < 8) {
        fprintf(stderr, "FAIL: counter fixture needs at least eight candidates\n");
        return false;
    }
    CUDA_CHECK(cudaMemcpyToSymbol(device_group, &group, sizeof group));
    const unsigned capacity = 4;
    wide untouched[capacity];
    memset(untouched, 0xa5, sizeof untouched);
    wide *device_survivors;
    counter_type *device_count;
    CUDA_CHECK(cudaMalloc(&device_survivors, sizeof untouched));
    CUDA_CHECK(cudaMalloc(&device_count, sizeof(counter_type)));
    CUDA_CHECK(cudaMemcpy(device_survivors, untouched, sizeof untouched, cudaMemcpyHostToDevice));
    counter_type count = static_cast<counter_type>((1ull << 32) - 4);
    CUDA_CHECK(cudaMemcpy(device_count, &count, sizeof count, cudaMemcpyHostToDevice));
    search_kernel<<<2, 32>>>(MODE_ALL, 0, 8, device_survivors, device_count, capacity);
    CUDA_CHECK(cudaGetLastError());
    CUDA_CHECK(cudaDeviceSynchronize());
    CUDA_CHECK(cudaMemcpy(&count, device_count, sizeof count, cudaMemcpyDeviceToHost));
    wide actual[capacity];
    CUDA_CHECK(cudaMemcpy(actual, device_survivors, sizeof actual, cudaMemcpyDeviceToHost));
    CUDA_CHECK(cudaFree(device_count));
    CUDA_CHECK(cudaFree(device_survivors));

    const unsigned long long expected = (1ull << 32) + 4;
    bool passed = true;
    if (static_cast<unsigned long long>(count) != expected) {
        fprintf(stderr, "FAIL: counter crossing 2^32: expected %llu, got %llu\n",
                expected, static_cast<unsigned long long>(count));
        passed = false;
    }
    if (memcmp(actual, untouched, sizeof actual) != 0) {
        fprintf(stderr, "FAIL: counter crossing 2^32 wrote survivors beyond capacity\n");
        passed = false;
    }
    if (passed) printf("PASS: counter crossing 2^32 = %llu; survivor buffer untouched\n", expected);
    return passed;
}

static bool is_palindrome(uint64_t value, unsigned base) {
    uint64_t reversed = 0;
    for (uint64_t remaining = value; remaining != 0; remaining /= base) {
        reversed = reversed * base + remaining % base;
    }
    return value == reversed;
}

static bool all_candidates_survive_splitting() {
    group_description group;
    if (!build_group(5, 3, &group)) {
        fprintf(stderr, "FAIL: cannot build candidate comparison group\n");
        return false;
    }
    /* Independent numeric enumeration avoids repeating the orbit construction. */
    std::vector<uint64_t> expected;
    for (uint64_t value = 1ull << (group.bit_length - 1); value < (1ull << group.bit_length); value++) {
        if (is_palindrome(value, 2) && is_palindrome(value, 16)) expected.push_back(value);
    }
    if (expected.size() != group.count || expected.size() <= 3) {
        fprintf(stderr, "FAIL: numeric reference disagrees with group size or cannot force splitting\n");
        return false;
    }

    bool passed = true;
    /* Exact capacity must succeed without dropping candidates; three forces retries. */
    const unsigned capacities[] = {static_cast<unsigned>(expected.size()), 3};
    for (unsigned capacity : capacities) {
        std::vector<wide> guarded(capacity + 2);
        memset(guarded.data(), 0xa5, guarded.size() * sizeof(wide));
        const wide sentinel = guarded.front();
        wide *device_allocation;
        counter_type *device_count;
        CUDA_CHECK(cudaMalloc(&device_allocation, guarded.size() * sizeof(wide)));
        CUDA_CHECK(cudaMalloc(&device_count, sizeof(counter_type)));
        CUDA_CHECK(cudaMemcpy(device_allocation, guarded.data(), guarded.size() * sizeof(wide), cudaMemcpyHostToDevice));
        std::vector<uint64_t> actual;
        std::vector<unsigned char> visits(static_cast<size_t>(group.count), 0);
        uint64_t visited = 0;
        bool valid_batches = true;
        uint64_t survivor_total = search_group_chunks(group, MODE_ALL, 2, 32,
                device_allocation + 1, device_count, capacity,
                [&](uint64_t begin, uint64_t end, const std::vector<wide> &survivors) {
            if (end > group.count || begin >= end || survivors.size() != end - begin || survivors.size() > capacity) {
                valid_batches = false;
                return;
            }
            visited += end - begin;
            for (uint64_t index = begin; index < end; index++) {
                if (visits[static_cast<size_t>(index)]++ != 0) valid_batches = false;
            }
            for (const wide &value : survivors) {
                for (int limb = 1; limb < LIMBS; limb++) if (value.limb[limb] != 0) valid_batches = false;
                actual.push_back(value.limb[0]);
            }
        });
        CUDA_CHECK(cudaMemcpy(guarded.data(), device_allocation, guarded.size() * sizeof(wide), cudaMemcpyDeviceToHost));
        CUDA_CHECK(cudaFree(device_count));
        CUDA_CHECK(cudaFree(device_allocation));
        std::sort(actual.begin(), actual.end());
        bool guards_intact = memcmp(&guarded.front(), &sentinel, sizeof(wide)) == 0 &&
                             memcmp(&guarded.back(), &sentinel, sizeof(wide)) == 0;
        if (!valid_batches || !guards_intact || visited != group.count || survivor_total != expected.size() || actual != expected) {
            fprintf(stderr, "FAIL: MODE_ALL capacity %u lost/duplicated candidates, credited retries, or damaged guards\n", capacity);
            passed = false;
        } else {
            printf("PASS: MODE_ALL capacity %u: %zu exact candidates, %llu visited, guards intact\n",
                   capacity, actual.size(), static_cast<unsigned long long>(visited));
        }
    }
    return passed;
}

int main() {
    bool counter_passed = counter_crosses_32_bits();
    bool splitting_passed = all_candidates_survive_splitting();
    return counter_passed && splitting_passed ? 0 : 1;
}
