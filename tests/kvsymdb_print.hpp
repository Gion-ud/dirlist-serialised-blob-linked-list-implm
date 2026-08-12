#pragma once
#include <iostream>
#include <stdio.h>
#include <kvsymdb.h>

namespace kvsymdb_print {
    using namespace cxx_kvsymdb;
    inline void print_entry_view(const kvsymdb::entry_view &entview_ref) {
        printf(
            "{\n"
            "\t%-12s: %u,\n"
            "\t%-12s: 0x%.8X,\n"
            "\t%-12s: 0x%.4X,\n"
            "\t%-12s: %u,\n"
            "\t%-12s: \"%.*s\",\n"
            "\t%-12s: %u,\n"
            "\t%-12s: \"%.*s\"\n"
            "}\n",
            "id",       entview_ref.id,
            "hash",     entview_ref.hash,
            "type",     entview_ref.type,
            "name_len", entview_ref.name_len,
            "name",     static_cast<int>(entview_ref.name_len), static_cast<const char*>(entview_ref.name),
            "data_len", entview_ref.data_len,
            "data",     static_cast<int>(entview_ref.data_len), static_cast<const char*>(entview_ref.data)
        );
    }
    inline void print_ent_kv(const kvsymdb::entry_view &entview_ref) {
        printf(
            "[%u] (0x%.8X, '%.*s', '%.*s')\n",
            entview_ref.id,
            entview_ref.hash,
            static_cast<int>(entview_ref.name_len),
            static_cast<const char*>(entview_ref.name),
            static_cast<int>(entview_ref.data_len),
            static_cast<const char*>(entview_ref.data)
        );
    }
    inline std::ostream &operator<<(
        std::ostream               &of_ref,
        const kvsymdb::buffer_view &str_view_ref
    ) {
        return of_ref.write(
            static_cast<const char*>(str_view_ref.data()),
            str_view_ref.length()
        );
    }
}