#ifndef APR_STREAM_ACCUMULATOR_HPP
#define APR_STREAM_ACCUMULATOR_HPP

#include <cstddef>

namespace apr {

// Accumulates bytes appended incrementally (e.g. from repeated async reads)
// and lets a consumer peek at / consume a prefix of the unconsumed data
// without repeatedly shifting the whole buffer.
//
// consume() only advances an internal offset (O(1)); the actual erase of
// already-consumed bytes is deferred to compact_if_needed(), which is cheap
// to call after processing all currently-available complete messages (O(1)
// when everything was consumed, and only O(remaining bytes) - not O(total
// bytes ever appended) - when some unconsumed data remains).
//
// Container must support: empty(), size(), clear(), insert(end(), first,
// last), erase(begin(), begin()+n), and expose value_type/data()/begin().
template <typename Container>
class stream_accumulator {
public:
    using value_type = typename Container::value_type;

    void append(const value_type* data, size_t n) {
        buf_.insert(buf_.end(), data, data + n);
    }

    const value_type* data() const { return buf_.data() + offset_; }
    size_t size() const { return buf_.size() - offset_; }
    bool empty() const { return size() == 0; }

    // Advances past n already-processed bytes. Does not itself deallocate;
    // call compact_if_needed() when done processing for this round.
    void consume(size_t n) {
        offset_ += n;
    }

    // Reclaims consumed space. Cheap (O(1)) when everything was consumed;
    // otherwise shifts only the remaining unconsumed bytes, and only once
    // the consumed prefix has grown past `threshold` to avoid compacting on
    // every single small consume().
    void compact_if_needed(size_t threshold = 4096) {
        if (offset_ == 0) return;
        if (offset_ == buf_.size()) {
            buf_.clear();
            offset_ = 0;
            return;
        }
        if (offset_ >= threshold) {
            buf_.erase(buf_.begin(), buf_.begin() + static_cast<typename Container::difference_type>(offset_));
            offset_ = 0;
        }
    }

    // Total bytes retained in the backing container (consumed + unconsumed) -
    // useful for enforcing a max-buffer-size cap regardless of compaction state.
    size_t total_capacity() const { return buf_.size(); }

    void clear() {
        buf_.clear();
        offset_ = 0;
    }

private:
    Container buf_;
    size_t offset_{0};
};

} // namespace apr

#endif // APR_STREAM_ACCUMULATOR_HPP
