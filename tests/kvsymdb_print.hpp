#pragma once
#include <iostream>
#include <stdio.h>
#include <kvsymdb.h>

namespace kvsymdb_print {
    using namespace cxx_kvsymdb;
    inline void print_entry_view(kvsymdb::entry_view &entview_r) {
        printf(
            "{\n"
            "\tid=%u,\n"
            "\thash=0x%.8X,\n"
            "\ttype=0x%.4X,\n"
            "\tname_len=%u,\n"
            "\tname='%s',\n"
            "\tdata_len=%u,\n"
            "\tdata='%.*s',\n"
            "\taddr=%p\n"
            "}\n",
            entview_r.id, entview_r.hash, entview_r.type,
            entview_r.name_len,
            static_cast<const char*>(entview_r.name),
            entview_r.data_len,
            static_cast<int>(entview_r.data_len),
            static_cast<const char*>(entview_r.data),
            entview_r._record
        );
    }
    inline void print_ent_kv(kvsymdb::entry_view &entview_r) {
        printf(
            "[%u] (0x%.8X, '%.*s', '%.*s')\n",
            entview_r.id,
            entview_r.hash,
            static_cast<int>(entview_r.name_len),
            static_cast<const char*>(entview_r.name),
            static_cast<int>(entview_r.data_len),
            static_cast<const char*>(entview_r.data)
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