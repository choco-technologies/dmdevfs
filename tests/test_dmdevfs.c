/**
 * @file test_dmdevfs.c
 * @brief Unit tests for DMDEVFS file system module
 * @author Automated Test Suite
 * 
 * This test suite validates the basic functionality of the dmdevfs module.
 */

#define DMOD_ENABLE_REGISTRATION
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "dmod.h"
#include "dmfsi.h"

// Test result tracking
typedef struct {
    int total;
    int passed;
    int failed;
} TestResults;

static TestResults test_results = {0};
static dmfsi_context_t fs_ctx = NULL;

// Test helper macros
#define TEST_START(name) \
    do { \
        printf("\n[TEST] %s...", name); \
        test_results.total++; \
    } while(0)

#define TEST_PASS() \
    do { \
        printf(" PASSED\n"); \
        test_results.passed++; \
    } while(0)

#define TEST_FAIL(reason) \
    do { \
        printf(" FAILED: %s\n", reason); \
        test_results.failed++; \
    } while(0)

#define ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            TEST_FAIL(message); \
            return false; \
        } \
    } while(0)

// -----------------------------------------
//      Test: Context initialization
// -----------------------------------------
bool test_context_init(const char* config_path)
{
    TEST_START("Context initialization");
    
    // Get the dmfsi init function
    dmod_dmfsi_init_t dmfsi_init = Dmod_GetDifFunction(
        Dmod_GetContext("dmdevfs"), 
        dmod_dmfsi_init_sig
    );
    
    ASSERT(dmfsi_init != NULL, "dmfsi_init function not found");
    
    fs_ctx = dmfsi_init(config_path);
    ASSERT(fs_ctx != NULL, "Failed to initialize filesystem context");
    
    TEST_PASS();
    return true;
}

// -----------------------------------------
//      Test: Context validation
// -----------------------------------------
bool test_context_validation(void)
{
    TEST_START("Context validation");
    
    dmod_dmfsi_context_is_valid_t dmfsi_context_is_valid = Dmod_GetDifFunction(
        Dmod_GetContext("dmdevfs"), 
        dmod_dmfsi_context_is_valid_sig
    );
    
    ASSERT(dmfsi_context_is_valid != NULL, "context_is_valid function not found");
    
    int valid = dmfsi_context_is_valid(fs_ctx);
    ASSERT(valid == 1, "Context validation failed");
    
    TEST_PASS();
    return true;
}

// -----------------------------------------
//      Test: File open operations
// -----------------------------------------
bool test_file_open(const char* test_path)
{
    TEST_START("File open (read-only)");
    
    dmod_dmfsi_fopen_t dmfsi_fopen = Dmod_GetDifFunction(
        Dmod_GetContext("dmdevfs"), 
        dmod_dmfsi_fopen_sig
    );
    
    ASSERT(dmfsi_fopen != NULL, "fopen function not found");
    
    void* fp = NULL;
    int ret = dmfsi_fopen(fs_ctx, &fp, test_path, DMFSI_O_RDONLY, 0);
    
    if (ret == DMFSI_OK && fp != NULL) {
        // Close the file
        dmod_dmfsi_fclose_t dmfsi_fclose = Dmod_GetDifFunction(
            Dmod_GetContext("dmdevfs"), 
            dmod_dmfsi_fclose_sig
        );
        
        if (dmfsi_fclose != NULL) {
            dmfsi_fclose(fs_ctx, fp);
        }
        TEST_PASS();
        return true;
    }
    
    TEST_FAIL("Cannot open file (this may be expected if no device is configured)");
    return false;
}

// -----------------------------------------
//      Test: File read operations
// -----------------------------------------
bool test_file_read(const char* test_path)
{
    TEST_START("File read operations");
    
    dmod_dmfsi_fopen_t dmfsi_fopen = Dmod_GetDifFunction(
        Dmod_GetContext("dmdevfs"), 
        dmod_dmfsi_fopen_sig
    );
    dmod_dmfsi_fread_t dmfsi_fread = Dmod_GetDifFunction(
        Dmod_GetContext("dmdevfs"), 
        dmod_dmfsi_fread_sig
    );
    dmod_dmfsi_fclose_t dmfsi_fclose = Dmod_GetDifFunction(
        Dmod_GetContext("dmdevfs"), 
        dmod_dmfsi_fclose_sig
    );
    
    ASSERT(dmfsi_fopen != NULL, "fopen function not found");
    ASSERT(dmfsi_fread != NULL, "fread function not found");
    ASSERT(dmfsi_fclose != NULL, "fclose function not found");
    
    void* fp = NULL;
    int ret = dmfsi_fopen(fs_ctx, &fp, test_path, DMFSI_O_RDONLY, 0);
    
    if (ret != DMFSI_OK || fp == NULL) {
        TEST_FAIL("Cannot open file for reading");
        return false;
    }
    
    char buffer[256] = {0};
    size_t bytes_read = 0;
    ret = dmfsi_fread(fs_ctx, fp, buffer, sizeof(buffer), &bytes_read);
    
    dmfsi_fclose(fs_ctx, fp);
    
    if (ret == DMFSI_OK) {
        printf(" (read %zu bytes)", bytes_read);
        TEST_PASS();
        return true;
    }
    
    TEST_FAIL("Read operation failed");
    return false;
}

// -----------------------------------------
//      Test: Directory operations
// -----------------------------------------
bool test_directory_operations(const char* dir_path)
{
    TEST_START("Directory operations");
    
    dmod_dmfsi_direxists_t dmfsi_direxists = Dmod_GetDifFunction(
        Dmod_GetContext("dmdevfs"), 
        dmod_dmfsi_direxists_sig
    );
    
    ASSERT(dmfsi_direxists != NULL, "direxists function not found");
    
    int exists = dmfsi_direxists(fs_ctx, dir_path);
    
    if (exists) {
        printf(" (directory exists)");
        TEST_PASS();
        return true;
    }
    
    TEST_FAIL("Directory does not exist or operation not supported");
    return false;
}

// -----------------------------------------
//      Test: Directory listing
// -----------------------------------------
bool test_directory_listing(const char* dir_path)
{
    TEST_START("Directory listing");
    
    dmod_dmfsi_opendir_t dmfsi_opendir = Dmod_GetDifFunction(
        Dmod_GetContext("dmdevfs"), 
        dmod_dmfsi_opendir_sig
    );
    dmod_dmfsi_readdir_t dmfsi_readdir = Dmod_GetDifFunction(
        Dmod_GetContext("dmdevfs"), 
        dmod_dmfsi_readdir_sig
    );
    dmod_dmfsi_closedir_t dmfsi_closedir = Dmod_GetDifFunction(
        Dmod_GetContext("dmdevfs"), 
        dmod_dmfsi_closedir_sig
    );
    
    ASSERT(dmfsi_opendir != NULL, "opendir function not found");
    ASSERT(dmfsi_readdir != NULL, "readdir function not found");
    ASSERT(dmfsi_closedir != NULL, "closedir function not found");
    
    void* dp = NULL;
    int ret = dmfsi_opendir(fs_ctx, &dp, dir_path);
    
    if (ret != DMFSI_OK || dp == NULL) {
        TEST_FAIL("Cannot open directory");
        return false;
    }
    
    dmfsi_dir_entry_t entry;
    int count = 0;
    printf("\n  Entries in %s:\n", dir_path);
    
    while (dmfsi_readdir(fs_ctx, dp, &entry) == DMFSI_OK) {
        printf("    - %s (size: %lu bytes)\n", entry.name, (unsigned long)entry.size);
        count++;
    }
    
    if (count == 0) {
        printf("    (empty directory)\n");
    } else {
        printf("  Total entries: %d\n", count);
    }
    
    dmfsi_closedir(fs_ctx, dp);
    
    TEST_PASS();
    return true;
}

// -----------------------------------------
//      Test: Context cleanup
// -----------------------------------------
bool test_context_cleanup(void)
{
    TEST_START("Context cleanup");
    
    if (fs_ctx == NULL) {
        TEST_FAIL("Context is already NULL");
        return false;
    }
    
    dmod_dmfsi_deinit_t dmfsi_deinit = Dmod_GetDifFunction(
        Dmod_GetContext("dmdevfs"), 
        dmod_dmfsi_deinit_sig
    );
    
    ASSERT(dmfsi_deinit != NULL, "deinit function not found");
    
    int ret = dmfsi_deinit(fs_ctx);
    fs_ctx = NULL;
    
    ASSERT(ret == DMFSI_OK, "Failed to deinitialize context");
    
    TEST_PASS();
    return true;
}

// -----------------------------------------
//      Run all tests
// -----------------------------------------
void run_all_tests(const char* config_path)
{
    printf("\n========================================\n");
    printf("  DMDEVFS Unit Test Suite\n");
    printf("========================================\n");
    printf("Config path: %s\n", config_path);
    
    // Test initialization
    if (!test_context_init(config_path)) {
        printf("\nFailed to initialize - skipping remaining tests\n");
        goto cleanup;
    }
    
    test_context_validation();
    
    // Test basic operations (these may fail if no devices are configured)
    test_directory_operations("/");
    test_directory_listing("/");
    
    // Note: File operations tests require actual device drivers to be configured
    // and available in the config path
    
    // Cleanup
    test_context_cleanup();
    
cleanup:
    // Print summary
    printf("\n========================================\n");
    printf("  Test Summary\n");
    printf("========================================\n");
    printf("Total tests:  %d\n", test_results.total);
    printf("Passed:       %d\n", test_results.passed);
    printf("Failed:       %d\n", test_results.failed);
    printf("========================================\n");
    
    if (test_results.failed == 0) {
        printf("\nResult: ✓ ALL TESTS PASSED\n");
    } else {
        printf("\nResult: ✗ SOME TESTS FAILED\n");
    }
    printf("\n");
}

// -----------------------------------------
//      Main function
// -----------------------------------------
int main(int argc, char *argv[])
{
    const char* config_path = "/tmp/test_config";
    
    // Parse command line arguments
    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            printf("Usage: %s [config_path]\n", argv[0]);
            printf("  config_path: Path to configuration directory (default: /tmp/test_config)\n");
            return 0;
        }
        config_path = argv[1];
    }
    
    // Load dmdevfs module
    Dmod_Context_t* context = Dmod_LoadModuleByName("dmdevfs");
    if (context == NULL) {
        printf("Cannot load dmdevfs module\n");
        return -1;
    }
    
    if (!Dmod_EnableModule("dmdevfs", true, NULL)) {
        printf("Cannot enable dmdevfs module\n");
        return -1;
    }
    
    printf("dmdevfs module loaded and enabled successfully.\n");
    
    // Create test config directory if it doesn't exist
    Dmod_MkDir(config_path);
    
    // Run test suite
    run_all_tests(config_path);
    
    // Cleanup
    Dmod_DisableModule("dmdevfs", false);
    
    return (test_results.failed > 0) ? 1 : 0;
}
