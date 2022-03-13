#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

// template <typename... Targs>
// void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    TCPHeader header = seg.header();
    string data = seg.payload().copy();
    bool eof = false;
    // listening for syn
    if (!_syn_flag && !header.syn)
        return;
    // recive first syn
    if (!_syn_flag && header.syn) {
        _syn_flag = true;
        _isn = header.seqno;
        if (header.fin) { // 第一次握手 FIN也可能为true
            _fin_flag = true;
            eof = true;
        }
        _reassembler.push_substring(data, 0, eof); // 握手时也可能携带数据
        return;
    }
    // 如果接收的seg FIN为true
    if (header.fin) {
        _fin_flag = true;
        eof = true;
    }
    // 要将seg header.seqno 转换为 stream index
    // chekpoint 是一个 abs_seqno 这里赋值为 已经收到的最后一个字节的abs_seqno（stream index + 1）
    uint64_t checkpoint = stream_out().bytes_written();
    uint64_t abs_seqno = unwrap(header.seqno, _isn, checkpoint);
    uint64_t stream_index = abs_seqno - 1;
    _reassembler.push_substring(data, stream_index, eof);
}

optional<WrappingInt32> TCPReceiver::ackno() const { 
    if (!_syn_flag)
        return nullopt;
    // abs_seqno是已经收到的字节数和SYN（SYN占用0号abs_seqno）
    uint64_t abs_seqno = stream_out().bytes_written();
    // 如果收到FIN且ByteStream关闭则FIN占用一个abs_seqno
    if (_fin_flag && stream_out().input_ended())
        ++ abs_seqno;
    return WrappingInt32(wrap(abs_seqno + 1, _isn)); 
}

size_t TCPReceiver::window_size() const { return stream_out().remaining_capacity(); }
