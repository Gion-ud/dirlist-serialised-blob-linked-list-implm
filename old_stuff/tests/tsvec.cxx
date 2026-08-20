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
    cxxds::static_vector<const char*, 32> v{};
    for (auto i = 0u; i < ktbl_len; ++i) {
        v.push_back(ktbl[i]);
    }
    puts("");
    for (auto it = v.begin(); it != v.end(); ++it) {
        std::cout << "[" << it - v.begin() << "] " << *it << '\n';
    }
    puts("");

    for (auto it = v.rbegin(); it != v.rend(); it = v.rnext(it)) {
        std::cout << "[" << it - v.begin() << "] " << v.deref(it) << '\n';
    }
    puts("");
    auto it = (v.*&cxxds::static_vector<const char*, 32>::begin)();
    std::cout << "[" << it - v.begin() << "] " << v.deref(it) << '\n';




    return 0;
}