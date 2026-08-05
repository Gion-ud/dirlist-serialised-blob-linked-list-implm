#include "jw_mem_stream.hpp"
#include <iostream>
#include <array>
#include <cassert>
#include "string_utils.hpp"
#include "array_utils.hpp"


const string_utils::string_view c_strview_tab[] = {
    "open",
    "read",
    "write",
    "lseek",
    "close",
    "getline",
    "putc",
    "getc",
    "tell",
    "seek",
    "find",
    "insert",
    "remove",
    "lookup",
    "get"
};

constexpr auto strtab = array_utils::c_array::make_array_view(c_strview_tab);


struct printer_iface {
    virtual void operator()() = 0;
    virtual void print() = 0;
    virtual ~printer_iface() = default;
    printer_iface() = default;
    printer_iface(const printer_iface &) = delete;
    printer_iface &operator=(const printer_iface &) = delete;
};

struct strview_printer : public printer_iface {
private:
    const string_utils::string_view &m_sv_ref;
public:
    strview_printer(const string_utils::string_view &sv_ref) noexcept :
        printer_iface(),
        m_sv_ref(sv_ref)    
    {
    }
    void print() noexcept override {
        using string_utils::operator<<;
        std::cout << m_sv_ref << std::endl;
    }
    void operator()() noexcept override {
        this->print();
    }
    printer_iface &base() {
        return *this;
    }
};


int main() {
    {
        using namespace string_utils;
        auto msg = string_view(make_string_literal("struct strview_printer : public printer_iface"));
        strview_printer print_msg(msg);
        print_msg.base().print();
        auto &print_msg_ref = print_msg.base();
        print_msg_ref();
    }

    std::array<uint8_t, 4096> buf{0};

    size_t len = 0UL;
    {
        auto *osp = jwio_cxx::buffer_ostream().base();

        osp->open(buf.data(), buf.size());
        assert(osp->is_open());

        for (const auto &str_ref : strtab) {
            size_t n = osp->write(str_ref.data(), str_ref.length());
            assert(n == str_ref.length());
            osp->putc('\n');
        }
        osp->putc('\0');
        len = osp->length();
    }

    {
        auto *isp = jwio_cxx::buffer_istream().base();
        isp->open(buf.data(), len);
        assert(isp->is_open());
        std::array<uint32_t, 8> smallbuf{};
        size_t nread = 0UL;
        using string_utils::operator<<;
        while ((nread = isp->read(smallbuf.data(), smallbuf.size())) != 0UL) {
            std::cout <<
                string_utils::string_view(
                    reinterpret_cast<const char*>(smallbuf.data()),
                    nread
                );
        }
        std::cout << std::endl;
    }


    return 0;
}