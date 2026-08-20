#include <cstdint>
#include <cstring>
#include <new>
#include <utility>
#include <memory>

namespace jwio_cxx {


template <typename DerivedStream>
struct _stream_common_iface {
protected:
    _stream_common_iface() = default;
    ~_stream_common_iface() = default;

    DerivedStream *to_derived() noexcept {
        return static_cast<DerivedStream*>(this);
    }

    const DerivedStream *to_derived() const noexcept {
        return static_cast<const DerivedStream*>(this);
    }
public:
    bool is_open() const noexcept {
        return this->to_derived()->is_open_impl();
    }
    void close() noexcept {
        return this->to_derived()->close_impl();
    }
    std::size_t seek(size_t offset) noexcept {
        return this->to_derived()->seek_impl(offset);
    }
    std::size_t tell() const noexcept {
        return this->to_derived()->tell_impl();
    }
    std::size_t length() const noexcept {
        return this->to_derived()->length_impl();
    }
    std::size_t size() const noexcept {
        return this->to_derived()->size_impl();
    }

    const uint8_t *data() const noexcept {
        return this->to_derived()->data_impl();
    }
    const uint8_t *begin() const noexcept {
        return this->to_derived()->begin_impl();
    }
    const uint8_t *current() const noexcept {
        return this->to_derived()->current_impl();
    }
    const uint8_t *end() const noexcept {
        return this->to_derived()->end_impl();
    }

};


template <typename DerivedIStream>
struct istream_iface :
    public _stream_common_iface<DerivedIStream>
{
protected:
    istream_iface() = default;
    ~istream_iface() = default;

public:
    int open(const void *mem, std::size_t size) noexcept {
        return this->to_derived()->open_impl(mem, size);
    }
    std::size_t read(void *dest_buf, std::size_t count) noexcept {
        return this->to_derived()->read_impl(dest_buf, count);
    }
    int getc() noexcept {
        return this->to_derived()->getc_impl();
    }
};


template <typename DerivedOStream>
struct ostream_iface :
    public _stream_common_iface<DerivedOStream>
{
protected:
    ostream_iface() = default;
    ~ostream_iface() = default;
public:
    int open(void *mem, std::size_t size) noexcept {
        return this->to_derived()->open_impl(mem, size);
    }
    std::size_t write(const void *src_buf, std::size_t count) noexcept {
        return this->to_derived()->write_impl(src_buf, count);
    }
    int putc(int ch) noexcept {
        return this->to_derived()->putc_impl(ch);
    }
    uint8_t *data() noexcept {
        return this->to_derived()->data_impl();
    }
    uint8_t *begin() noexcept {
        return this->to_derived()->begin_impl();
    }
    uint8_t *current() noexcept {
        return this->to_derived()->current_impl();
    }
    uint8_t *end() noexcept {
        return this->to_derived()->end_impl();
    }
};

struct buffer_istream : public istream_iface<buffer_istream> {
private:
    const uint8_t   *m_first;
    const uint8_t   *m_current;
    const uint8_t   *m_last;

public:
    istream_iface *base() noexcept {
        return this;
    }

    const uint8_t *begin_impl() const noexcept {
        return this->m_first;
    }
    const uint8_t *current_impl() const noexcept {
        return this->m_current;
    }
    const uint8_t *end_impl() const noexcept {
        return this->m_last;
    }

    std::size_t length_impl() const noexcept {
        return static_cast<std::size_t>(this->current() - this->begin());
    }
    std::size_t size_impl() const noexcept {
        return static_cast<std::size_t>(this->end() - this->begin());
    }
    const uint8_t *data_impl() const noexcept {
        return this->m_first;
    }

    buffer_istream(const void *buf, std::size_t size) noexcept :
        m_first(static_cast<const uint8_t*>(buf)),
        m_current(m_first),
        m_last(m_first + size)
    {
    }
    buffer_istream() noexcept :
        m_first(nullptr),
        m_current(nullptr),
        m_last(nullptr)
    {
    }
    void close_impl() noexcept {
        this->m_first   = nullptr;
        this->m_current = nullptr;
        this->m_last    = nullptr;
    }
    ~buffer_istream() noexcept {
        this->close();
    }
    istream_iface *base_impl() noexcept {
        return static_cast<istream_iface*>(this);
    }

    bool is_open_impl() const noexcept {
        return !!(this->m_first);
    }
    int open_impl(const void *mem, std::size_t size) noexcept {
        if (!mem || !size || this->is_open()) return -1;
        ::new (this) buffer_istream(mem, size);
        return 0;
    }

    std::size_t read_impl(void *dest_buf, std::size_t count) noexcept {
        std::size_t rem = static_cast<std::size_t>(this->end() - this->current());
        if (!rem) return 0UL;
        std::size_t readc = (count > rem) ? rem : count;
        std::memcpy(dest_buf, this->current(), readc);
        this->m_current += readc;
        return readc;
    }
    int getc_impl() noexcept {
        char ch = '\0';
        std::size_t n = this->read(&ch, 1);
        return (n == 1UL) ? static_cast<int>(ch) : -1;
    }
    std::size_t seek_impl(std::size_t offset) noexcept {
        if (offset > this->size()) offset = this->size();
        this->m_current = this->begin() + offset;
        return offset;
    }
    std::size_t tell_impl() const noexcept {
        return static_cast<std::size_t>(this->current() - this->begin());
    }
};

struct buffer_ostream : public ostream_iface<buffer_ostream> {
private:
    uint8_t    *m_first;
    uint8_t    *m_current;
    uint8_t    *m_last;

public:
    ostream_iface *base() noexcept {
        return this;
    }

    uint8_t *begin_impl() noexcept {
        return this->m_first;
    }
    uint8_t *current_impl() noexcept {
        return this->m_current;
    }
    uint8_t *end_impl() noexcept {
        return this->m_last;
    }

    const uint8_t *begin_impl() const noexcept {
        return this->m_first;
    }
    const uint8_t *current_impl() const noexcept {
        return this->m_current;
    }
    const uint8_t *end_impl() const noexcept {
        return this->m_last;
    }

    std::size_t length_impl() const noexcept {
        return static_cast<std::size_t>(this->current_impl() - this->begin_impl());
    }
    std::size_t size_impl() const noexcept {
        return static_cast<std::size_t>(this->end_impl() - this->begin_impl());
    }
    uint8_t *data_impl() noexcept {
        return this->m_first;
    }

    buffer_ostream(void *buf, std::size_t size) noexcept :
        m_first(static_cast<uint8_t*>(buf)),
        m_current(m_first),
        m_last(m_first + size)
    {
    }
    buffer_ostream() noexcept :
        m_first(nullptr),
        m_current(nullptr),
        m_last(nullptr)
    {
    }
    void close_impl() noexcept {
        this->m_first   = nullptr;
        this->m_current = nullptr;
        this->m_last    = nullptr;
    }
    ~buffer_ostream() noexcept {
        this->close();
    }
    ostream_iface *base_impl() noexcept {
        return static_cast<ostream_iface*>(this);
    }

    bool is_open_impl() const noexcept {
        return !!(this->m_first);
    }
    int open_impl(void *mem, std::size_t size) noexcept {
        if (!mem || !size || this->is_open()) return -1;
        ::new (this) buffer_ostream(mem, size);
        return 0;
    }
    std::size_t write_impl(const void *dest_buf, std::size_t count) noexcept {
        std::size_t rem = static_cast<std::size_t>(this->end() - this->current());
        if (!rem) return 0UL;
        std::size_t writec = (count > rem) ? rem : count;
        std::memcpy(this->current(), dest_buf, writec);
        this->m_current += writec;
        return writec;
    }
    int putc_impl(int ch) noexcept {
        char _ch = static_cast<char>(ch);
        std::size_t n = this->write(&_ch, 1);
        return (n == 1UL) ? static_cast<int>(ch) : -1;
    }
    std::size_t seek_impl(std::size_t offset) noexcept {
        if (offset > this->size()) offset = this->size();
        this->m_current = this->begin() + offset;
        return offset;
    }
    std::size_t tell_impl() const noexcept {
        return static_cast<std::size_t>(this->current_impl() - this->begin_impl());
    }
};



} // namespace jwio


