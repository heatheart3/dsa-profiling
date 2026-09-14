#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define BLOCK_SIZE (64U * 1024U)

static uint64_t monotonic_ns(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void pin_to_cpu(unsigned int cpu)
{
    cpu_set_t set;

    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if (sched_setaffinity(0, sizeof(set), &set) != 0) {
        perror("sched_setaffinity");
        exit(EXIT_FAILURE);
    }
}

static unsigned long parse_ulong(const char *value, const char *name)
{
    char *end;
    unsigned long result;

    errno = 0;
    result = strtoul(value, &end, 10);
    if (errno != 0 || *value == '\0' || *end != '\0') {
        fprintf(stderr, "invalid %s: %s\n", name, value);
        exit(EXIT_FAILURE);
    }
    return result;
}

int main(int argc, char **argv)
{
    unsigned long seconds = 5;
    unsigned long source_mib = 512;
    unsigned long cpu = 0;
    size_t source_size;
    unsigned char *source;
    unsigned char *destination;
    uint64_t start;
    uint64_t now;
    uint64_t deadline;
    uint64_t operations = 0;
    uint64_t bytes;
    off_t offset = 0;
    int fd;
    size_t page_size;
    size_t i;
    volatile unsigned int checksum = 0;

    if (argc > 4) {
        fprintf(stderr, "usage: %s [seconds [source-MiB [cpu]]]\n", argv[0]);
        return EXIT_FAILURE;
    }
    if (argc > 1)
        seconds = parse_ulong(argv[1], "seconds");
    if (argc > 2)
        source_mib = parse_ulong(argv[2], "source-MiB");
    if (argc > 3)
        cpu = parse_ulong(argv[3], "cpu");
    if (seconds == 0 || source_mib == 0) {
        fprintf(stderr, "seconds and source-MiB must be nonzero\n");
        return EXIT_FAILURE;
    }

    source_size = source_mib * 1024UL * 1024UL;
    source_size -= source_size % BLOCK_SIZE;
    if (source_size < BLOCK_SIZE) {
        fprintf(stderr, "source must contain at least one 64 KiB block\n");
        return EXIT_FAILURE;
    }

    pin_to_cpu((unsigned int)cpu);
    fd = memfd_create("cached-read-bw", MFD_CLOEXEC);
    if (fd < 0) {
        perror("memfd_create");
        return EXIT_FAILURE;
    }
    if (ftruncate(fd, (off_t)source_size) != 0) {
        perror("ftruncate");
        return EXIT_FAILURE;
    }

    source = mmap(NULL, source_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (source == MAP_FAILED) {
        perror("mmap");
        return EXIT_FAILURE;
    }
    page_size = (size_t)sysconf(_SC_PAGESIZE);
    for (i = 0; i < source_size; i += page_size)
        source[i] = (unsigned char)(i / page_size);
    if (munmap(source, source_size) != 0) {
        perror("munmap");
        return EXIT_FAILURE;
    }

    if (posix_memalign((void **)&destination, page_size, BLOCK_SIZE) != 0) {
        fprintf(stderr, "posix_memalign failed\n");
        return EXIT_FAILURE;
    }
    memset(destination, 0, BLOCK_SIZE);

    /* Warm the page-cache source and the destination mapping before timing. */
    for (offset = 0; offset < (off_t)source_size; offset += BLOCK_SIZE) {
        if (pread(fd, destination, BLOCK_SIZE, offset) != BLOCK_SIZE) {
            perror("warmup pread");
            return EXIT_FAILURE;
        }
    }

    offset = 0;
    start = monotonic_ns();
    deadline = start + seconds * 1000000000ULL;
    now = start;
    do {
        for (i = 0; i < 1024; i++) {
            ssize_t copied = pread(fd, destination, BLOCK_SIZE, offset);

            if (copied != BLOCK_SIZE) {
                if (copied < 0)
                    perror("pread");
                else
                    fprintf(stderr, "short pread: %zd\n", copied);
                return EXIT_FAILURE;
            }
            checksum += destination[0];
            operations++;
            offset += BLOCK_SIZE;
            if (offset == (off_t)source_size)
                offset = 0;
        }
        now = monotonic_ns();
    } while (now < deadline);

    bytes = operations * BLOCK_SIZE;
    printf("cached pread: block=64KiB source=%luMiB cpu=%lu ops=%llu "
           "latency=%.3fus bandwidth=%.3fGB/s (%.3fGiB/s) checksum=%u\n",
           source_mib, cpu, (unsigned long long)operations,
           (double)(now - start) / 1000.0 / (double)operations,
           (double)bytes / ((double)(now - start) / 1e9) / 1e9,
           (double)bytes / ((double)(now - start) / 1e9) /
               (1024.0 * 1024.0 * 1024.0),
           checksum);

    free(destination);
    close(fd);
    return EXIT_SUCCESS;
}
