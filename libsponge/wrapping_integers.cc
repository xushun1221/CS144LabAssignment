#include "wrapping_integers.hh"

// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    // seqno = isn + n % 2^32
    return WrappingInt32{isn + static_cast<uint32_t>(n)};
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    uint64_t offset = n.raw_value() - isn.raw_value();
    // checkpoint = A * 2^32 + B
    uint64_t A = checkpoint >> 32;
    // num1 = (A - 1) * 2^32 + offset
    uint64_t num1 = ((A - 1) << 32) + offset;
    uint64_t abs1 = checkpoint - num1;
    // num2 = A * 2^32 + offset
    uint64_t num2 = (A << 32) + offset;
    uint64_t abs2 = checkpoint > num2 ? checkpoint - num2 : num2 - checkpoint;
    // num3 = (A + 1) * 2^32 + offset
    uint64_t num3 = ((A + 1) << 32) + offset;
    uint64_t abs3 = num3 - checkpoint;
    // A == 0 只需比较abs2 abs3
    if (A == 0)
        return abs2 < abs3 ? num2 : num3;
    // 比较abs1 abs2 abs3
    if (abs1 < abs2)
        return abs1 < abs3 ? num1 : num3;
    return abs2 < abs3 ? num2 : num3;
}
