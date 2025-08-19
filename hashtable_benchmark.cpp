/**
 * @file hashtable_benchmark.cpp
 * @brief Performance benchmarks for YANET hashtables
 */

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <rte_common.h>

// Include YANET hashtable headers
#include "dataplane/hashtable.h"

// Define the log priority variable that's referenced in dataplane/hashtable.h
common::log::LogPriority common::log::logPriority = common::log::TLOG_DEBUG;

// Test configuration
#define NUM_REPETITIONS 10
#define NUM_THREADS 8
#define L3_CACHE_SIZE (32 * 1024 * 1024) // 32MB typical L3 cache
#define VALUE_SIZE 64 // 64 bytes per value
#define TOTAL_VALUES (L3_CACHE_SIZE / VALUE_SIZE * 8) // 8x L3 cache size for more intensive test
#define TOTAL_OPS (TOTAL_VALUES * NUM_THREADS * NUM_REPETITIONS)

// Global hashtable size constants
constexpr uint32_t HASHTABLE_SIZE_T = TOTAL_VALUES / 4; // Primary buckets
constexpr uint32_t HASHTABLE_EXTENDED_SIZE_T = TOTAL_VALUES / 4; // Extended buckets
constexpr unsigned int HASHTABLE_PAIRS_PER_CHUNK_T = 4;
constexpr unsigned int HASHTABLE_PAIRS_PER_EXTENDED_CHUNK_T = 4;

// Test data structures
typedef struct
{
	int key;
	char value[VALUE_SIZE];
} test_entry_t;

// Thread data structure
typedef struct
{
	void* hashtable;
	int thread_id;
	uint8_t value_seed;
	double elapsed_time;
	uint64_t write_checksum;
	uint64_t read_checksum;
	int successful_writes;
	int successful_reads;
} thread_data_t;

// ANSI color codes for output
#define COLOR_RESET "\033[0m"
#define COLOR_RED "\033[31m"
#define COLOR_GREEN "\033[32m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_BLUE "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN "\033[36m"
#define COLOR_WHITE "\033[37m"
#define COLOR_BOLD "\033[1m"
static char buf[32];

/**
 * @brief Format a number in human-readable form with appropriate units
 * @param num The number to format
 * @return Pointer to the formatted string
 */
static inline char*
format_number(size_t num)
{
	const char* units[] = {"", "K", "M", "G", "T"};
	int unit_index = 0;
	double value = (double)num;

	while (value >= 1000.0 && unit_index < 4)
	{
		value /= 1000.0;
		unit_index++;
	}

	if (unit_index == 0)
	{
		snprintf(buf, sizeof(buf), "%zu", num);
	}
	else if (value == (int)value)
	{
		snprintf(buf, sizeof(buf), "%d%s", (int)value, units[unit_index]);
	}
	else
	{
		snprintf(buf, sizeof(buf), "%.1f%s", value, units[unit_index]);
	}

	return buf;
}

static double
get_time(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1000000000.0;
}

/**
 * Writer thread for hashtable_chain_spinlock_t
 */
static void*
writer_thread_chain_spinlock(void* arg)
{
	thread_data_t* data = (thread_data_t*)arg;

	using hashtable_type = dataplane::hashtable_chain_spinlock_t<int, test_entry_t, HASHTABLE_SIZE_T, HASHTABLE_EXTENDED_SIZE_T, HASHTABLE_PAIRS_PER_CHUNK_T, HASHTABLE_PAIRS_PER_EXTENDED_CHUNK_T>;
	auto* ht = static_cast<hashtable_type*>(data->hashtable);

	test_entry_t entry;
	memset(entry.value, data->value_seed, VALUE_SIZE);
	entry.value[VALUE_SIZE - 1] = '\0'; // Ensure null termination

	double start_time = get_time();
	int successful = 0;
	data->write_checksum = 0; // Initialize checksum

	int j = 0;
	for (; j < NUM_REPETITIONS; j++)
	{
		for (int i = 0; i < TOTAL_VALUES; i++)
		{
			int key = i;
			entry.key = key;

			int id = key % NUM_THREADS;
			entry.value[id] = id;
			if (ht->insert(key, entry))
			{
				if (j == 0)
				{
					if (id == data->thread_id)
					{
						data->write_checksum += key + id + data->value_seed;
					}
				}
				successful++;
			}
			else
			{
				printf("L%d ERROR failed to write value for %d", __LINE__, key);
				exit(1);
			}
		}
	}

	double end_time = get_time();
	data->elapsed_time = end_time - start_time;

	// Write to per-thread counters (no contention)
	data->successful_writes = successful;
	return NULL;
}

/**
 * Reader thread for hashtable_chain_spinlock_t
 */
static void*
reader_thread_chain_spinlock(void* arg)
{
	thread_data_t* data = (thread_data_t*)arg;

	using hashtable_type = dataplane::hashtable_chain_spinlock_t<int, test_entry_t, HASHTABLE_SIZE_T, HASHTABLE_EXTENDED_SIZE_T, HASHTABLE_PAIRS_PER_CHUNK_T, HASHTABLE_PAIRS_PER_EXTENDED_CHUNK_T>;
	auto* ht = static_cast<hashtable_type*>(data->hashtable);

	double start_time = get_time();
	int successful = 0;
	data->read_checksum = 0; // Initialize checksum

	int j = 0;
	for (; j < NUM_REPETITIONS; j++)
	{
		// Then perform the benchmark reads from all ranges
		for (int i = 0; i < TOTAL_VALUES; i++)
		{
			int key = i;

			test_entry_t* found_value = nullptr;
			dataplane::spinlock_t* locker = nullptr;

			ht->lookup(key, found_value, locker);

			// Always unlock first to avoid deadlocks
			if (found_value)
			{

				if (j == 0)
				{
					int id = key % NUM_THREADS;
					if (id == data->thread_id)
					{
						data->read_checksum += key + found_value->value[data->thread_id] + data->value_seed;
					}
				}
				successful++;
			}
			else
			{
				printf("L%d ERROR: value with key=%d is not found\n", __LINE__, key);
				exit(1);
			}

			if (locker)
			{
				locker->unlock();
				locker = nullptr;
			}
		}
	}

	double end_time = get_time();
	data->elapsed_time = end_time - start_time;

	// Write to per-thread counters (no contention)
	data->successful_reads = successful;
	return NULL;
}

/**
 * Writer thread for hashtable_mod_spinlock_dynamic
 */
static void*
writer_thread_mod_spinlock(void* arg)
{
	thread_data_t* data = (thread_data_t*)arg;

	using hashtable_type = dataplane::hashtable_mod_spinlock_dynamic<int, test_entry_t, 8, dataplane::calculate_hash_crc<int>>;
	auto* ht = static_cast<hashtable_type*>(data->hashtable);

	test_entry_t entry;
	memset(entry.value, data->value_seed, VALUE_SIZE);
	entry.value[VALUE_SIZE - 1] = '\0'; // Ensure null termination

	double start_time = get_time();
	int successful = 0;
	data->write_checksum = 0; // Initialize checksum

	int j = 0;
	for (; j < NUM_REPETITIONS; j++)
	{
		for (int i = 0; i < TOTAL_VALUES; i++)
		{
			int key = i;
			entry.key = key;

			int id = key % NUM_THREADS;
			entry.value[id] = id;
			// Use insert_or_update which handles locking internally and always returns true
			if (ht->insert_or_update(key, entry))
			{
				if (j == 0)
				{
					if (id == data->thread_id)
					{
						data->write_checksum += key + id + data->value_seed;
					}
				}
				successful++;
			}
			else
			{
				printf("L%d ERROR failed to write value for %d\n", __LINE__, key);
				exit(1);
			}
		}
	}

	double end_time = get_time();
	data->elapsed_time = end_time - start_time;

	// Write to per-thread counters (no contention)
	data->successful_writes = successful;
	return NULL;
}

/**
 * Reader thread for hashtable_mod_spinlock_dynamic
 */
static void*
reader_thread_mod_spinlock(void* arg)
{
	thread_data_t* data = (thread_data_t*)arg;

	using hashtable_type = dataplane::hashtable_mod_spinlock_dynamic<int, test_entry_t, 8, dataplane::calculate_hash_crc<int>>;
	auto* ht = static_cast<hashtable_type*>(data->hashtable);

	double start_time = get_time();
	int successful = 0;
	data->read_checksum = 0; // Initialize checksum

	int j = 0;
	for (; j < NUM_REPETITIONS; j++)
	{
		// Then perform the benchmark reads from all ranges
		for (int i = 0; i < TOTAL_VALUES; i++)
		{
			int key = i;

			test_entry_t* found_value = nullptr;
			dataplane::spinlock_nonrecursive_t* locker = nullptr;

			ht->lookup(key, found_value, locker);

			// Always unlock first to avoid deadlocks
			if (found_value)
			{

				if (j == 0)
				{
					int id = key % NUM_THREADS;
					if (id == data->thread_id)
					{
						data->read_checksum += key + found_value->value[data->thread_id] + data->value_seed;
					}
				}
				successful++;
			}
			else
			{
				printf("L%d ERROR: value with key=%d is not found\n", __LINE__, key);
				exit(1);
			}

			if (locker)
			{
				locker->unlock();
				locker = nullptr;
			}
		}
	}

	double end_time = get_time();
	data->elapsed_time = end_time - start_time;

	// Write to per-thread counters (no contention)
	data->successful_reads = successful;
	return NULL;
}

/**
 * Test hashtable_chain_spinlock_t multithreaded performance
 */
void test_hashtable_chain_spinlock_mt()
{
	printf(COLOR_GREEN "\n\n=== chain_spinlock_t " COLOR_MAGENTA "Multi-threaded ===" COLOR_RESET "\n");

	// Using larger sizes for better performance test
	constexpr uint32_t size_T = 32768; // Increased from 256
	constexpr uint32_t extendedSize_T = 32768; // Increased from 256
	constexpr unsigned int pairsPerChunk_T = 4;
	constexpr unsigned int pairsPerExtendedChunk_T = 4;

	// Use heap allocation to avoid stack overflow - the hashtable is too large for stack
	using hashtable_type = dataplane::hashtable_chain_spinlock_t<int, test_entry_t, HASHTABLE_SIZE_T, HASHTABLE_EXTENDED_SIZE_T, HASHTABLE_PAIRS_PER_CHUNK_T, HASHTABLE_PAIRS_PER_EXTENDED_CHUNK_T>;

	size_t required_size = sizeof(hashtable_type);
	void* memory = calloc(required_size, 1);
	if (!memory)
	{
		printf("Failed to allocate memory for hashtable\n");
		return;
	}

	hashtable_type* ht = new (memory) hashtable_type();

	printf("  Hashtable key slots: %s\n",
	       format_number(dataplane::hashtable_chain_spinlock_t<int, test_entry_t, size_T, extendedSize_T, pairsPerChunk_T, pairsPerExtendedChunk_T>::keysSize));
	printf("\n");

	uint8_t value_seed = (uint8_t)rand();

	// Create thread data
	pthread_t threads[NUM_THREADS];
	thread_data_t thread_data[NUM_THREADS];

	// Phase 1: Concurrent writes
	double write_start = get_time();

	for (int i = 0; i < NUM_THREADS; i++)
	{
		thread_data[i].hashtable = ht;
		thread_data[i].value_seed = value_seed;
		thread_data[i].thread_id = i;

		if (pthread_create(&threads[i], NULL, writer_thread_chain_spinlock, &thread_data[i]) != 0)
		{
			printf("Failed to create writer thread %d\n", i);
			return;
		}
	}

	// Wait for all writer threads to complete
	for (int i = 0; i < NUM_THREADS; i++)
	{
		pthread_join(threads[i], NULL);
	}

	double write_end = get_time();
	double total_write_time_sec = write_end - write_start;
	double total_write_elapsed_time = 0.0;

	// Sum up per-thread write statistics
	int total_successful_writes = 0;
	for (int i = 0; i < NUM_THREADS; i++)
	{
		total_successful_writes += thread_data[i].successful_writes;
		total_write_elapsed_time += thread_data[i].elapsed_time;
	}

	printf(COLOR_YELLOW "+ Write Phase Results +" COLOR_RESET "\n");
	printf("Total write time(with joins): %.3f seconds\n", total_write_time_sec);
	printf("Elapsed write time: %.3f seconds\n", total_write_elapsed_time);
	printf("Total write operations: %s\n", format_number(TOTAL_OPS));
	printf("Successful writes: %s\n", format_number(total_successful_writes));
	printf("Write throughput: " COLOR_CYAN "%s ops/sec" COLOR_RESET "\n", format_number((double)TOTAL_OPS / total_write_elapsed_time));
	printf("Write success rate: %d/%d\n", total_successful_writes, TOTAL_OPS);

	// Get hashtable statistics
	auto stats = ht->stats();
	printf("\nHashtable statistics after writes:\n");
	printf("  Total pairs: %s\n", format_number(stats.pairs));
	printf("  Extended chunks count: %s\n", format_number(stats.extendedChunksCount));
	printf("  Longest chain: %s\n", format_number(stats.longestChain));
	printf("  Insert failed: %s\n", format_number(stats.insertFailed));

	double read_start = get_time();
	for (int i = 0; i < NUM_THREADS; i++)
	{
		thread_data[i].elapsed_time = 0;
		thread_data[i].read_checksum = 0; // Reset read checksum

		if (pthread_create(&threads[i], NULL, reader_thread_chain_spinlock, &thread_data[i]) != 0)
		{
			printf("Failed to create reader thread %d\n", i);
			return;
		}
	}

	// Wait for all reader threads to complete
	for (int i = 0; i < NUM_THREADS; i++)
	{
		pthread_join(threads[i], NULL);
	}

	double read_end = get_time();
	double total_read_time_sec = read_end - read_start;

	// Sum up per-thread read statistics
	int total_successful_reads = 0;
	uint64_t read_checksum = 0;
	double total_read_elapsed_time = 0.0;
	for (int i = 0; i < NUM_THREADS; i++)
	{
		total_successful_reads += thread_data[i].successful_reads;
		read_checksum += thread_data[i].read_checksum;
		total_read_elapsed_time += thread_data[i].elapsed_time;
	}

	printf(COLOR_YELLOW "+ Read Phase Results +" COLOR_RESET "\n");
	printf("Wall read time: %.3f seconds\n", total_read_time_sec);
	printf("Elapsed read CPU time (sum): %.3f seconds\n", total_read_elapsed_time);
	printf("Total read operations: %s\n", format_number(TOTAL_OPS));
	printf("Read checksum: %zu\n", read_checksum);
	printf("Successful reads: %s\n", format_number(total_successful_reads));
	printf("Read throughput: " COLOR_CYAN "%s ops/sec" COLOR_RESET "\n", format_number((double)TOTAL_OPS / total_read_elapsed_time));
	printf("Read success rate: %d/%d\n", total_successful_reads, TOTAL_OPS);

	// Compare checksums to verify data integrity
	for (int i = 0; i < NUM_THREADS; i++)
	{
		if (thread_data[i].write_checksum != thread_data[i].read_checksum)
		{
			printf("L%d ERROR: Checksum mismatch for thread %d: write=%lu, read=%lu\n",
			       __LINE__,
			       i,
			       thread_data[i].write_checksum,
			       thread_data[i].read_checksum);
			exit(1);
		}
	}

	// Add assertions to fail the test if success rates are not 100%
	// Compare actual counts instead of percentages to avoid floating point precision issues
	if (total_successful_writes != TOTAL_OPS)
	{
		printf("L%d ERROR: Write success rate (%d/%d) is below required threshold\n", __LINE__, total_successful_writes, TOTAL_OPS);
		exit(1);
	}
	if (total_successful_reads != TOTAL_OPS)
	{
		printf("L%d ERROR: Read success rate (%d/%d) is below required threshold\n", __LINE__, total_successful_reads, TOTAL_OPS);
		exit(1);
	}

	// Clean up heap-allocated hashtable
	ht->~hashtable_type();
	free(memory);
}

/**
 * Test hashtable_mod_spinlock basic performance
 */
void test_hashtable_mod_spinlock_basic()
{
	printf(COLOR_GREEN "\n\n=== mod_spinlock Single-threaded ===" COLOR_RESET "\n");

	// Use dynamic allocation to avoid stack overflow
	const uint32_t total_size = TOTAL_VALUES;
	constexpr uint32_t chunk_size = 8;

	// Calculate required memory size
	size_t required_size = dataplane::hashtable_mod_spinlock_dynamic<int, test_entry_t, chunk_size, dataplane::calculate_hash_crc<int>>::calculate_sizeof(total_size);
	printf("Required memory size: %s bytes\n", format_number(required_size));

	// Allocate memory for the hashtable (use calloc to zero-initialize)
	void* memory = calloc(1, required_size);
	if (!memory)
	{
		printf("Failed to allocate memory for hashtable\n");
		return;
	}

	// Use placement new to construct the hashtable
	dataplane::hashtable_mod_spinlock_dynamic<int, test_entry_t, chunk_size, dataplane::calculate_hash_crc<int>>* ht =
	        new (memory) dataplane::hashtable_mod_spinlock_dynamic<int, test_entry_t, chunk_size, dataplane::calculate_hash_crc<int>>();

	// Initialize the hashtable using updater
	typename dataplane::hashtable_mod_spinlock_dynamic<int, test_entry_t, chunk_size, dataplane::calculate_hash_crc<int>>::updater updater;
	updater.update_pointer(ht, 0, total_size);

	// Clear/initialize the hashtable properly
	ht->clear();

	printf("  Hashtable pairs: %s\n", format_number(total_size));
	printf("\n");

	// Phase 1: Sequential writes
	double write_start = get_time();

	int successful_writes = 0;
	uint64_t write_checksum = 0; // Prevent compiler optimization
	for (int i = 0; i < TOTAL_VALUES; i++)
	{
		test_entry_t entry;
		entry.key = i;
		memset(entry.value, 'A' + (i % 26), VALUE_SIZE);
		entry.value[VALUE_SIZE - 1] = '\0'; // Ensure null termination

		// First do a lookup to get the hash, then insert
		test_entry_t* found_value = nullptr;
		dataplane::spinlock_nonrecursive_t* locker = nullptr;
		uint32_t hash = ht->lookup(i, found_value, locker);
		write_checksum += hash; // Use hash to prevent optimization
		if (locker)
		{
			locker->unlock();
		}

		if (ht->insert(hash, i, entry))
		{
			successful_writes++;
		}
	}

	double write_end = get_time();
	double total_write_time_sec = write_end - write_start;

	printf(COLOR_YELLOW "+ Write Phase Results +" COLOR_RESET "\n");
	printf("Total write time: %.3f seconds\n", total_write_time_sec);
	printf("Write checksum: %s\n", format_number(write_checksum));
	printf("Successful writes: %s\n", format_number(successful_writes));
	printf("Write throughput: " COLOR_CYAN "%s ops/sec" COLOR_RESET "\n", format_number((size_t)((double)TOTAL_VALUES / total_write_time_sec)));
	printf("Write success rate: %d/%d\n", successful_writes, TOTAL_VALUES);

	// Phase 2: Sequential reads
	double read_start = get_time();
	int successful_reads = 0;
	uint64_t read_checksum = 0; // Prevent compiler optimization

	for (int i = 0; i < TOTAL_VALUES; i++)
	{
		test_entry_t* found_value = nullptr;
		dataplane::spinlock_nonrecursive_t* locker = nullptr;

		ht->lookup(i, found_value, locker);
		if (found_value)
		{
			if (found_value->key == i)
			{
				// Verify the value pattern
				if (found_value->value[0] == 'A' + (i % 26))
				{
					successful_reads++;
					read_checksum += found_value->key + found_value->value[0]; // Use data to prevent optimization
				}
				else
				{
					printf("failed to read value with key=%d, value missmatch\n", i);
				}
			}
		}
		// Always unlock if we have a locker
		if (locker)
		{
			locker->unlock();
		}
	}

	double read_end = get_time();
	double total_read_time_sec = read_end - read_start;

	printf("\n");
	printf(COLOR_YELLOW "+ Read Phase Results +" COLOR_RESET "\n");
	printf("Total read time: %.3f seconds\n", total_read_time_sec);
	printf("Read checksum: %zu\n", read_checksum);
	printf("Successful reads: %s\n", format_number(successful_reads));
	printf("Read throughput: " COLOR_CYAN "%s ops/sec" COLOR_RESET "\n", format_number((size_t)(successful_writes / total_read_time_sec)));
	printf("Read success rate: %d/%d\n", successful_reads, successful_writes);

	// Add assertions to fail the test if success rates are not 100%
	// Compare actual counts instead of percentages to avoid floating point precision issues
	if (successful_writes != TOTAL_VALUES)
	{
		printf("L%d ERROR: Write success rate (%d/%d) is below required threshold\n", __LINE__, successful_writes, TOTAL_VALUES);
		exit(1);
	}
	if (successful_reads != successful_writes)
	{
		printf("L%d ERROR: Read success rate (%d/%d) is below required threshold\n", __LINE__, successful_reads, successful_writes);
		exit(1);
	}

	// Clean up
	free(memory);
}

/**
 * Test hashtable_mod_spinlock multithreaded performance
 */
void test_hashtable_mod_spinlock_mt()
{
	printf(COLOR_GREEN "\n\n=== mod_spinlock " COLOR_MAGENTA "Multi-threaded ===" COLOR_RESET "\n");

	// Use dynamic allocation to avoid stack overflow
	const uint32_t total_size = TOTAL_VALUES;
	constexpr uint32_t chunk_size = 8;

	// Calculate required memory size
	size_t required_size = dataplane::hashtable_mod_spinlock_dynamic<int, test_entry_t, chunk_size, dataplane::calculate_hash_crc<int>>::calculate_sizeof(total_size);

	// Allocate memory for the hashtable (use calloc to zero-initialize)
	void* memory = calloc(1, required_size);
	if (!memory)
	{
		printf("Failed to allocate memory for hashtable\n");
		return;
	}

	// Use placement new to construct the hashtable
	dataplane::hashtable_mod_spinlock_dynamic<int, test_entry_t, chunk_size, dataplane::calculate_hash_crc<int>>* ht =
	        new (memory) dataplane::hashtable_mod_spinlock_dynamic<int, test_entry_t, chunk_size, dataplane::calculate_hash_crc<int>>();

	// Initialize the hashtable using updater
	typename dataplane::hashtable_mod_spinlock_dynamic<int, test_entry_t, chunk_size, dataplane::calculate_hash_crc<int>>::updater updater;
	updater.update_pointer(ht, 0, total_size);

	// Clear/initialize the hashtable properly
	ht->clear();

	printf("  Hashtable pairs: %s\n", format_number(total_size));
	printf("\n");

	uint8_t value_seed = (uint8_t)rand();

	// Create thread data
	pthread_t threads[NUM_THREADS];
	thread_data_t thread_data[NUM_THREADS];

	// Phase 1: Concurrent writes
	double write_start = get_time();

	for (int i = 0; i < NUM_THREADS; i++)
	{
		thread_data[i].hashtable = ht;
		thread_data[i].value_seed = value_seed;
		thread_data[i].thread_id = i;

		if (pthread_create(&threads[i], NULL, writer_thread_mod_spinlock, &thread_data[i]) != 0)
		{
			printf("Failed to create writer thread %d\n", i);
			return;
		}
	}

	// Wait for all writer threads to complete
	for (int i = 0; i < NUM_THREADS; i++)
	{
		pthread_join(threads[i], NULL);
	}

	double write_end = get_time();
	double total_write_time_sec = write_end - write_start;

	// Sum up per-thread write statistics
	int total_successful_writes = 0;
	uint64_t write_checksum = 0;
	double total_write_elapsed_time = 0.0;
	for (int i = 0; i < NUM_THREADS; i++)
	{
		write_checksum += thread_data[i].write_checksum;
		total_successful_writes += thread_data[i].successful_writes;
		total_write_elapsed_time += thread_data[i].elapsed_time;
	}

	printf(COLOR_YELLOW "+ Write Phase Results +" COLOR_RESET "\n");
	printf("Wall write time(with joins): %.3f seconds\n", total_write_time_sec);
	printf("Elapsed write CPU time (sum): %.3f seconds\n", total_write_elapsed_time);
	printf("Write checksum: %s\n", format_number(write_checksum));
	printf("Successful writes: %s\n", format_number(total_successful_writes));
	printf("Write throughput: " COLOR_CYAN "%s ops/sec" COLOR_RESET "\n", format_number((double)TOTAL_OPS / total_write_elapsed_time));
	printf("Write success rate: %d/%d\n", total_successful_writes, TOTAL_OPS);

	// Phase 2: Concurrent reads
	double read_start = get_time();
	for (int i = 0; i < NUM_THREADS; i++)
	{
		thread_data[i].elapsed_time = 0;

		if (pthread_create(&threads[i], NULL, reader_thread_mod_spinlock, &thread_data[i]) != 0)
		{
			printf("Failed to create reader thread %d\n", i);
			return;
		}
	}

	// Wait for all reader threads to complete
	for (int i = 0; i < NUM_THREADS; i++)
	{
		pthread_join(threads[i], NULL);
	}

	double read_end = get_time();
	double total_read_time_sec = read_end - read_start;

	// Sum up per-thread read statistics
	int total_successful_reads = 0;
	uint64_t read_checksum = 0;
	double total_read_elapsed_time = 0.0;
	for (int i = 0; i < NUM_THREADS; i++)
	{
		total_successful_reads += thread_data[i].successful_reads;
		read_checksum += thread_data[i].read_checksum;
		total_read_elapsed_time += thread_data[i].elapsed_time;
	}

	printf(COLOR_YELLOW "+ Read Phase Results +" COLOR_RESET "\n");
	printf("Wall read time: %.3f seconds\n", total_read_time_sec);
	printf("Elapsed read CPU time (sum): %.3f seconds\n", total_read_elapsed_time);
	printf("Total read operations: %s\n", format_number(TOTAL_OPS));
	printf("Read checksum: %zu\n", read_checksum);
	printf("Successful reads: %s\n", format_number(total_successful_reads));
	printf("Read throughput: " COLOR_CYAN "%s ops/sec" COLOR_RESET "\n", format_number((size_t)(TOTAL_OPS / total_read_elapsed_time)));

	printf("Read success rate: %d/%d\n", total_successful_reads, TOTAL_OPS);

	// Compare checksums to verify data integrity
	for (int i = 0; i < NUM_THREADS; i++)
	{
		if (thread_data[i].write_checksum != thread_data[i].read_checksum)
		{
			printf("L%d ERROR: Checksum mismatch for thread %d: write=%lu, read=%lu\n",
			       __LINE__,
			       i,
			       thread_data[i].write_checksum,
			       thread_data[i].read_checksum);
			exit(1);
		}
	}

	// Add assertions to fail the test if success rates are not 100%
	// Compare actual counts instead of percentages to avoid floating point precision issues
	if (total_successful_writes != TOTAL_OPS)
	{
		printf("L%d ERROR: Write success rate (%d/%d) is below required threshold\n", __LINE__, total_successful_writes, TOTAL_OPS);
		exit(1);
	}
	if (total_successful_reads != TOTAL_OPS)
	{
		printf("L%d ERROR: Read success rate (%d/%d) is below required threshold\n", __LINE__, total_successful_reads, TOTAL_OPS);
		exit(1);
	}

	// Clean up
	free(memory);
}

int main()
{
	printf("\n\nConfiguration:\n");
	printf("  Threads: %d\n", NUM_THREADS);
	printf("  Total values: %s\n", format_number(TOTAL_VALUES));
	printf("  Value size: %d bytes\n", VALUE_SIZE);
	printf("  Total data size: %d MB (%dx L3 cache)\n",
	       (TOTAL_VALUES * VALUE_SIZE) / (1024 * 1024),
	       (TOTAL_VALUES * VALUE_SIZE) / L3_CACHE_SIZE);
	// Run the multithreaded performance test for hashtable_chain_spinlock_t
	test_hashtable_chain_spinlock_mt();
	// Run the basic performance test for hashtable_mod_spinlock
	test_hashtable_mod_spinlock_basic();
	// Run the multithreaded performance test for hashtable_mod_spinlock
	test_hashtable_mod_spinlock_mt();
}
