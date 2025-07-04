#include <libc.h>

// Function to swap two elements
static void swap(void* a, void* b, size_t size) {
    char temp[size];
    memcpy(temp, a, size);
    memcpy(a, b, size);
    memcpy(b, temp, size);
}

// Partition function
static int partition(void* base, size_t size, int (*cmp)(const void*, const void*), int low, int high) {
    char* arr = (char*)base;
    void* pivot = arr + high * size; // Choosing the last element as pivot
    int i = low - 1; // Index of smaller element

    for (int j = low; j < high; j++) {
        if (cmp(arr + j * size, pivot) <= 0) {
            i++;
            swap(arr + i * size, arr + j * size, size);
        }
    }
    swap(arr + (i + 1) * size, arr + high * size, size);
    return (i + 1);
}

// Quick Sort function
static void quick_sort(void* base, size_t size, int (*cmp)(const void*, const void*), int low, int high) {
    if (low < high) {
        int pi = partition(base, size, cmp, low, high);
        quick_sort(base, size, cmp, low, pi - 1);
        quick_sort(base, size, cmp, pi + 1, high);
    }
}

// Public interface function
void qsort(void* ptr, size_t count, size_t size, int (*comp)(const void*, const void*)) {
    quick_sort(ptr, size, comp, 0, count - 1);
}
