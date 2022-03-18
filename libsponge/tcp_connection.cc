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

size_t TCPConnection::time_since_last_segment_received() const { return _time_since_last_segment_received_ms; }

void TCPConnection::segment_received(const TCPSegment &seg) { 
    // DUMMY_CODE(seg); 
    // 收到seg重启计时
    _time_since_last_segment_received_ms = 0;
    // _receiver接收seg
    _receiver.segment_received(seg);
    // 收到rst 直接断开连接
    if (seg.header().rst == true) {
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        _linger_after_streams_finish = false;
        _active_flag = false;
        return;
    }
    // 判断是否需要发送一个空seg 来keep-alive
    // 如果对方发来的seg是空包 则不需要返回一个空包
    bool need_epmty_seg = seg.length_in_sequence_space();
    // 如果收到了ack包 则更新_sender的状态
    if (seg.header().ack == true) {
        _sender.ack_received(seg.header().ackno, seg.header().win);
        // 如果_sender.segment_out()中有数据 那么就不需要发送空包
        if (need_epmty_seg && !_sender.segments_out().empty())
            need_epmty_seg = false;
    }
    // receiver处于LISTEN状态并收到SYN
    // sender处于CLOSE状态从未发送数据
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::SYN_RECV)
        if (TCPState::state_summary(_sender) == TCPSenderStateSummary::CLOSED) {
            // 发送 syn + ack 
            // 这是第一次调用 fill_window()
            connect();
            return;
        }
    // 收到对方发来的 FIN 但是还有数据要发送 不立即断开连接
    // CLOSE-WAIT
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV)
        if (TCPState::state_summary(_sender) == TCPSenderStateSummary::SYN_ACKED)
            _linger_after_streams_finish = false;
    // CLOSED
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV)
        if (TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED && !_linger_after_streams_finish) {
            _active_flag = false;
            return;
        }
    // 如果seg里没有负载 这个seg只是为了keep-alive
    // _sender.segment_out()没有数据就发送一个空包
    if (need_epmty_seg)
        _sender.send_empty_segment();
}

bool TCPConnection::active() const { return _active_flag; }

size_t TCPConnection::write(const string &data) {
    // DUMMY_CODE(data);
    size_t written_bytes = _sender.stream_in().write(data);
    _sender.fill_window();
    return written_bytes;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) { 
    // DUMMY_CODE(ms_since_last_tick); 
    // 自上次收到seg经过了多少时间
    _time_since_last_segment_received_ms += ms_since_last_tick;
    // 经过时间发送给sender
    _sender.tick(ms_since_last_tick);
    // 如果连续重传次数超过最大重传次数
    // 认为连接中断 发送rst
    if (_sender.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        TCPSegment rst_seg;
        rst_seg.header().rst = true;
        _segments_out.push(rst_seg);
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        _linger_after_streams_finish = false;
        _active_flag = false;
        return;
    }
    // 发送队列中的seg 并加上 ack 和 ackno
    while (!_sender.segments_out().empty()) {
        TCPSegment front_seg = _sender.segments_out().front();
        front_seg.header().win = _receiver.window_size();
        if (_receiver.ackno().has_value()) {
            front_seg.header().ack = true;
            front_seg.header().ackno = _receiver.ackno().value();
        }
        _segments_out.push(front_seg);
        _sender.segments_out().pop();
    }
    // 如果处于 TIME-WAIT 状态并超时 则关闭连接
    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV)
        if (TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED)
            if (_time_since_last_segment_received_ms >= 10 * _cfg.rt_timeout) {
                _active_flag = false;
                _linger_after_streams_finish = false;
            }
}

void TCPConnection::end_input_stream() { 
    _sender.stream_in().end_input(); 
    _sender.fill_window();
}

void TCPConnection::connect() {
    _sender.fill_window();
    _active_flag = true;
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";
            // Your code here: need to send a RST segment to the peer
            TCPSegment rst_seg;
            rst_seg.header().rst = true;
            _segments_out.push(rst_seg);
            _sender.stream_in().set_error();
            _receiver.stream_out().set_error();
            _linger_after_streams_finish = false;
            _active_flag = false;
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
