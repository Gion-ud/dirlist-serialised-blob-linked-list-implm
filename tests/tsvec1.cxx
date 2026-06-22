#include <cxxds_vector.hpp>
#include <iostream>
#include <stddef.h>
#include <assert.h>
#include <array>
#include <vector>
#include <algorithm>

const char *ktbl[] {
    "new",
    "delete",
    "libgcc_s",
    "libgcc",
    "libstdc++",
    "libllvm",
    "open",
    "close",
    "read",
    "write",
    "lseek",
    "opendir",
    "closedir",
    "mmap",
    "fstat",
    "fopen",
    "fclose",
    "fseek",
    "ftell"
};

uint_t ktbl_len = sizeof(ktbl) / sizeof(*ktbl);

int main() {
    cxxds::vector<const char*> vec{};
    assert(vec.is_valid());
    vec.reserve(32);

    for (auto i = 0u; i < ktbl_len; ++i) {
        vec.push_back(ktbl[i]);
    }

    for (auto it = vec.begin(); it != vec.end(); it = vec.next(it)) {
        std::cout << *it << '\n';
    }
    puts("");

    cxxds::vector<const char*> v2{};
    vec.move_out(&v2);
    assert(!vec.is_valid() && v2.is_valid());

    for (auto it = v2.begin(); it != v2.end(); it = v2.next(it)) {
        std::cout << *it << '\n';
    }
    puts("");

    vec.move_in(&v2);
    assert(vec.is_valid() && !v2.is_valid());
    for (auto it = vec.begin(); it != vec.end(); it = vec.next(it)) {
        std::cout << *it << '\n';
    }
    puts("");

    cxxds::vector<const char*> v1{};
    v1.copy_from(&vec);
    assert(vec.is_valid() && v1.is_valid());

    for (auto it = v1.begin(); it != v1.end(); it = v1.next(it)) {
        std::cout << *it << '\n';
    }
    puts("");
    v1.pop_back();
    v1.pop_back();
    v1.pop_back();
    v1.pop_back();
    v1.pop_back();
    v1.pop_back();
    puts("");
    for (auto it = v1.begin(); it != v1.end(); it = v1.next(it)) {
        std::cout << v1.deref(it) << '\n';
    }
    puts("");
    for (auto it = vec.begin(); it != vec.end(); it = vec.next(it)) {
        std::cout << "[" << it - vec.begin() << "] " << *it << '\n';
    }
    puts("");
    for (auto i = 0u; i < vec.size(); ++i) {
        std::cout << vec[i] << '\n';
    }
    puts("");
    for (auto it = vec.rbegin(); it != vec.rend(); it = vec.rnext(it)) {
        std::cout << "[" << it - vec.begin() << "] " << vec.deref(it) << '\n';
    }
    puts("");
    for (auto i = 0u; i < vec.size(); ++i) {
        std::cout << *vec.get(i) << '\n';
    }
    puts("");
    std::cout << vec.front() << '\n';
    std::cout << vec.back() << '\n';
    std::cout << "*(vec.end() - 1) = " << *(vec.end() - 1) << '\n';

    vec.clear();

    vec.push_back("std::vector");
    vec.push_back("std::vector::iterator");
    vec.push_back("std::array");
    vec.push_back("std::addressof");


    puts("");
    for (auto i = 0u; i < vec.size(); ++i) {
        std::cout << *vec.get(i) << '\n';
    }
    vec._destroy();
    v1._destroy();


    cxxds::vector<int> v{};
    for (cxxds::vector<int>::iterator it = v.begin(); it != v.end(); ++it) {
        std::cout << v.deref(it) << '\n';
    }
    puts("");
    std::array<int, 8> arr = { 9, 4, 5, 67, 69, 3, 11, 13 };
    for (auto i = 0u; i < arr.size(); ++i) {
        v.push_back(arr[i]);
    }
    v.push_back(19);
    puts("");
    for (auto i = 0u; i < v.size(); ++i) {
        std::cout << *v.get(i) << '\n';
    }
    std::sort(v.begin(), v.end());
    puts("");
    for (auto i = 0u; i < v.size(); ++i) {
        std::cout << *v.get(i) << '\n';
    }
    puts("");
    for (auto it = v.rbegin(); it != v.rend(); it = v.rnext(it)) {
        std::cout << *it << '\n';
    }
    puts("");
    puts("cleared");

    return 0;
}