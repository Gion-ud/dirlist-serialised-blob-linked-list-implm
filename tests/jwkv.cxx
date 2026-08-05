#include <jwkvmap.hpp>
#include <iostream>
#include <cstring>
#include <cassert>
#include "array_utils.hpp"
#include <cstdint>


constexpr uint32_t KVSIG = 0xFF00FF00u;

namespace jwkvmap_tests {
    struct cstr_kv {
        const char *name;
        const char *data;
    };

    using namespace jwkvmap;
    using namespace array_utils;

    template<std::size_t ArrLen>
    inline int insert_from_kvarr(
        KVMap  &kvm_ref,
        const c_array::array_view<const cstr_kv, ArrLen> &kvarr_ref
    ) noexcept {
        using namespace fmt;
        std::clog << "[INFO] insert_from_kvarr @ BEGIN" << std::endl;
        for (const auto &ent_ref : kvarr_ref) {
            std::cout << "[INFO] it " << &ent_ref - kvarr_ref.begin() << std::endl;
            auto rc = kvm_ref.insert(
                ent_ref.name,
                ent_ref.data,
                KVSIG
            );
            if (rc) {
                std::cerr
                    << "kvm.insert failed: "
                    << KVError(kvm_ref.geterror())
                    << std::endl;
                return JWKVMAP_FAILED;
            }
        }
        std::clog << "[INFO] insert_from_kvarr @ END\n" << std::endl;
        return JWKVMAP_OK;
    }

    template<std::size_t ArrLen>
    inline int insert_auto_rehash_from_kvarr(
        KVMap  &kvm_ref,
        const c_array::array_view<const cstr_kv, ArrLen> &kvarr_ref
    ) noexcept {
        using namespace fmt;    
        std::clog << "[INFO] insert_auto_rehash_from_kvarr @ BEGIN" << std::endl;
        for (const auto &ent_ref : kvarr_ref) {
            std::cout << "[INFO] it " << &ent_ref - kvarr_ref.begin() << std::endl;
            auto rc = kvm_ref.insert_auto_rehash(
                ent_ref.name,
                ent_ref.data,
                KVSIG
            );
            if (rc) {
                std::cerr
                    << "kvm.insert_auto_rehash failed: "
                    << KVError(kvm_ref.geterror())
                    << std::endl;
                return JWKVMAP_FAILED;
            }
        }
        std::clog << "[INFO] insert_auto_rehash_from_kvarr @ END\n" << std::endl;
        return JWKVMAP_OK;
    }

    template<std::size_t ArrLen>
    inline int find_and_print_keys(
        KVMap  &kvm_ref,
        const c_array::array_view<const cstr_kv, ArrLen> &kvarr_ref
    ) noexcept {
        using namespace fmt;
        std::clog << "[INFO] find_and_print_keys @ BEGIN" << std::endl;
        for (const auto &ent_ref : kvarr_ref) {
            const auto *ent_p = kvm_ref[ent_ref.name];
            if (!ent_p) {
                std::cout
                    << "kvm.find(): "
                    << jwkvmap::KVError(kvm_ref.geterror())
                    << std::endl;
                return JWKVMAP_FAILED;
            }

            auto ev = jwkvmap::KVMap::EntryView(ent_p);
            std::cout
                << "ent.hash="
                << jwkvmap::fmt::u32_lower_hex_fmt(ev.hash()) << "\n"
                << "ent.type="
                << jwkvmap::fmt::u32_upper_hex_fmt(ev.type()) << "\n"
                << "ent.key='" << ev.key() << "'\n"
                << "ent.value='" << ev.value() << "'\n\n";
        }

        std::clog << "[INFO] find_and_print_keys @ END\n" << std::endl;
        return JWKVMAP_OK;
    }

    template<std::size_t ArrLen>
    inline int find_and_remove_keys(
        KVMap  &kvm_ref,
        const c_array::array_view<const cstr_kv, ArrLen> &kvarr_ref
    ) noexcept {
        using namespace fmt;
        std::clog << "[INFO] find_and_remove_keys @ BEGIN" << std::endl;
        for (const auto &ent_ref : kvarr_ref) {
            int rc = kvm_ref.remove(ent_ref.name);
            if (rc) {
                std::cout
                    << "kvm.remove(): "
                    << jwkvmap::KVError(kvm_ref.geterror())
                    << std::endl;
                return JWKVMAP_FAILED;
            }
        }

        std::clog << "[INFO] find_and_remove_keys @ END\n" << std::endl;
        return JWKVMAP_OK;
    }


    struct KVMapSizeFmt {
    private:
        const KVMap &m_kvm_ref;
    public:
        KVMapSizeFmt(const KVMap &kvm_ref) noexcept :
            m_kvm_ref(kvm_ref)
        {
        }
        friend std::ostream &operator<<(
            std::ostream       &os_ref,
            const KVMapSizeFmt &kvms_ref
        );
    };

    std::ostream &operator<<(
        std::ostream       &os_ref,
        const KVMapSizeFmt &kvms_ref
    ) {
        os_ref
            << "ht size: " << kvms_ref.m_kvm_ref.bucket_count() << '\n'
            << "ht entc: " << kvms_ref.m_kvm_ref.slot_count() << '\n';
        return os_ref;
    }

}

static constexpr jwkvmap_tests::cstr_kv _rec_arr[] = {
    {"open", "libc fcntl.h: open"},
    {"close", "libc unistd.h: close"},
    {"read", "libc unistd.h: read"},
    {"write", "libc unistd.h: write"},
    {"lseek", "libc unistd.h: lseek"},
    {"mmap", "libc sys/mman.h: mmap"},
    {"munmap", "libc sys/mman.h: munmap"},
    {"opendir", "libc dirent.h: opendir"},
    {"closedir", "libc dirent.h: closedir"},
    {"readdir", "libc dirent.h: readdir"},
    {"seekdir", "libc dirent.h: seekdir"},
    {"telldir", "libc dirent.h: telldir"},
    {"malloc", "libc stdlib.h: malloc"},
    {"calloc", "libc stdlib.h: calloc"},
    {"realloc", "libc stdlib.h: realloc"},
    {"free", "libc stdlib.h: free"},
    {"getdents64", "(x86-64_linux, GNU libc) sys/syscall.h: getdents64"},
    {"db_open", "(BSD) libdb db.h: db_open"},
    {"vsnprintf", "libc stdio.h: vsnprintf"},
    {"new", "libstdc++ new: operator new"},
    {"delete", "libstdc++ new: operator delete"},
};

constexpr const auto kvarr = array_utils::c_array::make_array_view(_rec_arr);


int main() {
    using jwkvmap_tests::operator<<;
    using namespace jwkvmap_tests;
    using jwkvmap::fmt::operator<<;


    std::cout << "1. -----[INIT]-----"  << std::endl;
    jwkvmap::KVMap kvm{};
    kvm.reserve(8UL);
    std::cout << KVMapSizeFmt(kvm) << std::endl;

    std::cout << "2. -----insert all-----"  << std::endl;
    int rc = insert_from_kvarr(kvm, kvarr);
    assert(!rc);
    std::cout << std::endl;

    std::cout << "3. -----insert all again-----"  << std::endl;
    rc = insert_from_kvarr(kvm, kvarr);
    assert(rc);
    std::cout << std::endl;

    std::cout << "4.---------find and print"  << std::endl;
    rc = find_and_print_keys(kvm, kvarr);
    assert(!rc);
    std::cout << std::endl;
    std::cout << KVMapSizeFmt(kvm) << std::endl;

    std::cout << "5.----------delete"  << std::endl;
    rc = find_and_remove_keys(kvm, kvarr);
    assert(!rc);

    std::cout << "6.---------rehash and put"  << std::endl;
    rc = insert_auto_rehash_from_kvarr(kvm, kvarr);
    assert(!rc);
    std::cout << std::endl;

    std::cout << "7.---------find and print"  << std::endl;
    rc = find_and_print_keys(kvm, kvarr);
    assert(!rc);
    std::cout << std::endl;
    std::cout << KVMapSizeFmt(kvm) << std::endl;

    return 0;
}


