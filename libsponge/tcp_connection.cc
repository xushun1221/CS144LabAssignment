#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return _sender.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return _sender.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return _receiver.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received; }

void TCPConnection::segment_received(const TCPSegment &seg) { 
    if (!active())
        return;
    // 收到rst消息 需要立即关闭连接
    if (seg.header().rst == true) {
        //unclean_shutdown();
        return;
    }
    // LISTEN 没有收到syn之前 收到的seg不处理
    if (!seg.header().syn&& !_receiver.ackno().has_value())
        return;
    // 如果seg里有ack信息 则将确认信息告知sender
    if (seg.header().ack)
        _sender.ack_received(seg.header().ackno, seg.header().win);
    // 接收的seg交由receiver处理
    _receiver.segment_received(seg);
    // 收到seg我们需要对其进行确认 确认信息由发送的seg捎带 所以要填充窗口
    // 更新了确认信息后 应该马上填充窗口  syn会在这里添加
    _sender.fill_window();
    // 如果要对seg进行ack 如果发送队列里没有数据 我们发送一个空seg来确认
    // 还要注意如果接收的seg本身就是空的 那我们不能对ack进行ack 跳过
    if (seg.length_in_sequence_space() > 0 && _sender.segments_out().empty())
        _sender.send_empty_segment();
    // ack ackno win 会在这里添加
    send_segments();
}

bool TCPConnection::active() const { return _active_flag; }

size_t TCPConnection::write(const string &data) {
    if (!active())
        return 0;
    size_t written_bytes = _sender.stream_in().write(data);
    _sender.fill_window();
    send_segments();
    return written_bytes;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) { 
    if (!active())
        return;
    _time_since_last_segment_received = 0;
    _sender.tick(ms_since_last_tick);
    if (_sender.consecutive_retransmissions() > _cfg.MAX_RETX_ATTEMPTS)
        unclean_shutdown();
    else {
        send_segments();
        try_clean_shutdown();
    }
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    send_segments();
}

void TCPConnection::connect() {
    // 第一次fill会发送syn
    _sender.fill_window();
    send_segments();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            // Your code here: need to send a RST segment to the peer
            unclean_shutdown();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}

void TCPConnection::send_segments() {
    TCPSegment seg;
    while (!_sender.segments_out().empty()) {
        seg = _sender.segments_out().front();
        _sender.segments_out().pop();
        if (_receiver.ackno().has_value()) {
            seg.header().ack = true;
            seg.header().ackno = _receiver.ackno().value();
            seg.header().win = _receiver.window_size();  // ??
        }
        _segments_out.push(seg);
    }
}

void TCPConnection::try_clean_shutdown() {
    if (_receiver.stream_out().input_ended() && !(_sender.stream_in().eof())) {
        _linger_after_streams_finish = false;
    }
    if (_receiver.stream_out().input_ended() && _sender.stream_in().eof() && _sender.bytes_in_flight() == 0) {
        if (!_linger_after_streams_finish || time_since_last_segment_received() >= 10 * _cfg.rt_timeout) {
            _active_flag = false; // 就把自己关了
        }
    }
}

void TCPConnection::unclean_shutdown() {
    // 关闭输入输出流
    _sender.stream_in().set_error();
    _receiver.stream_out().set_error();
    // 关闭active状态
    _active_flag = false;
    // 构建 rst 消息
    TCPSegment seg;
    // 立即发送rst信号
    // 如果当前发送队列里面还有seg 那就把rst捎带发送
    if (!_sender.segments_out().empty()) {
        seg = _sender.segments_out().front();
        _sender.segments_out().pop();
        seg.header().rst = true;
        // 不能忘了捎带ackno
        if (_receiver.ackno().has_value()) {
            seg.header().ack = true;
            seg.header().ackno = _receiver.ackno().value();
            seg.header().win = _receiver.window_size();
        }
    }
    // 如果没有数据可以发送 发送空seg
    if (_sender.segments_out().empty()) {
        seg.header().rst = true;
        seg.header().seqno = _sender.next_seqno();
    }
    // 发送 rst
    _segments_out.push(seg);
}