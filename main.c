#include <windows.h>
#include <stdio.h>

// Define the NTSTATUS type and status codes
typedef LONG NTSTATUS;
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000DL)
#define STATUS_INTEGER_OVERFLOW ((NTSTATUS)0xC0000095L)
#define STATUS_ACCESS_VIOLATION ((NTSTATUS)0xC0000005L)

// Function pointer type for RtlCharToInteger
typedef NTSTATUS(NTAPI* RtlCharToInteger_t)(
    PCSTR String,
    ULONG Base,
    PULONG Value
    );

// Custom implementation of RtlCharToInteger with overflow checking
NTSTATUS NTAPI SafeRtlCharToInteger(
    PCSTR String,
    ULONG Base,
    PULONG Value
) {
    CHAR chCurrent;
    int digit;
    ULONGLONG RunningTotal = 0; // Use 64-bit to detect overflow
    char bMinus = 0;

    // Validate inputs
    if (String == NULL) return STATUS_INVALID_PARAMETER;
    if (Value == NULL) return STATUS_ACCESS_VIOLATION;

    // Skip leading whitespace
    while (*String != '\0' && *String <= ' ') String++;

    // Handle sign
    if (*String == '+') {
        String++;
    }
    else if (*String == '-') {
        bMinus = 1;
        String++;
    }

    // Base auto-detection
    if (Base == 0) {
        Base = 10;
        if (String[0] == '0' && String[1] != '\0') {
            if (String[1] == 'b') { String += 2; Base = 2; }
            else if (String[1] == 'o') { String += 2; Base = 8; }
            else if (String[1] == 'x') { String += 2; Base = 16; }
        }
    }
    else if (Base != 2 && Base != 8 && Base != 10 && Base != 16) {
        return STATUS_INVALID_PARAMETER;
    }

    // Process digits
    int digit_count = 0;
    while (*String != '\0') {
        chCurrent = *String;

        // Convert character to digit
        if (chCurrent >= '0' && chCurrent <= '9') {
            digit = chCurrent - '0';
        }
        else if (chCurrent >= 'A' && chCurrent <= 'Z') {
            digit = chCurrent - 'A' + 10;
        }
        else if (chCurrent >= 'a' && chCurrent <= 'z') {
            digit = chCurrent - 'a' + 10;
        }
        else {
            digit = -1;
        }

        // Validate digit
        if (digit < 0 || digit >= (int)Base) break;

        // Check for overflow
        if (RunningTotal > ULONG_MAX / Base || (RunningTotal * Base > ULONG_MAX - digit)) {
            return STATUS_INTEGER_OVERFLOW;
        }

        RunningTotal = RunningTotal * Base + digit;
        String++;
        digit_count++;
    }

    // Check if any valid digits were processed
    if (digit_count == 0) return STATUS_INVALID_PARAMETER;

    // Apply sign and store result
    if (bMinus && RunningTotal > (ULONGLONG)((LONG_MAX)+1)) {
        return STATUS_INTEGER_OVERFLOW; // Negative overflow check
    }
    *Value = bMinus ? (0 - (ULONG)RunningTotal) : (ULONG)RunningTotal;
    return STATUS_SUCCESS;
}

int main() {
    // Load ntdll.dll
    HMODULE ntdll = LoadLibraryA("ntdll.dll");
    if (!ntdll) {
        printf("Failed to load ntdll.dll: %lu\n", GetLastError());
        return 1;
    }

    // Dynamically resolve RtlCharToInteger
    RtlCharToInteger_t pRtlCharToInteger = (RtlCharToInteger_t)GetProcAddress(ntdll, "RtlCharToInteger");
    if (!pRtlCharToInteger) {
        printf("Failed to resolve RtlCharToInteger: %lu\n", GetLastError());
        FreeLibrary(ntdll);
        return 1;
    }

    // Test cases for POC
    struct {
        const char* input;
        ULONG base;
        const char* description;
    } tests[] = {
        {"123", 10, "Normal decimal input"},
        {"-123", 10, "Negative decimal input"},
        {"0x1A", 0, "Hexadecimal with auto-detection"},
        {"4294967296", 10, "Overflow case (2^32)"}, // Should overflow
        {"0x100000000", 16, "Hex overflow case (2^32)"},
        {"", 10, "Empty string"},
        {"0x", 16, "Invalid hex string"},
        {NULL, 10, "NULL string"}
    };

    printf("=== Testing Original RtlCharToInteger ===\n");
    for (int i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        ULONG result = 0;
        NTSTATUS status = STATUS_INVALID_PARAMETER;

        // Handle NULL string safely
        __try {
            status = pRtlCharToInteger(tests[i].input, tests[i].base, &result);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            status = STATUS_ACCESS_VIOLATION;
        }

        printf("Test: %s\n", tests[i].description);
        printf("Input: %s, Base: %lu, Status: 0x%08X, Result: %lu (0x%08X)\n",
            tests[i].input ? tests[i].input : "NULL", tests[i].base, status, result, result);
        printf("\n");
    }


    // Free the library
    FreeLibrary(ntdll);
    return 0;
}
