#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

// template <typename... Targs>
// void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
    , _retransmission_timer(retx_timeout) {}

uint64_t TCPSender::bytes_in_flight() const { return _retrans_buffer_space; }

void TCPSender::fill_window() {
    // 尽管接收方的窗口大小为0 还是要发送报文段来获得最新窗口大小
    // 我们需要用另一个变量来表示window_size
    // 因为如果我们令window_size == 1 会导致二进制指数退避
    size_t win_size = _window_size == 0 ? 1 : _window_size;
    // 已发送未确认的字节数小于窗口大小才可以继续发送
    while (_retrans_buffer_space < win_size) {
        // 构造新的seg
        TCPSegment new_seg;
        // 如果是第一次发送 需要syn = true
        if (!_syn_flag) {
            new_seg.header().syn = true;
            _syn_flag = true;
        }
        // 装入序列号
        new_seg.header().seqno = next_seqno();
        // 待装入的负载长度 尽可能长  syn占一位seqno fin 同样也需要一位 如果当前payload正好没装下fin 那么fin就由下一个seg单独发送
        size_t payload_size = min(TCPConfig::MAX_PAYLOAD_SIZE, win_size - _retrans_buffer_space) - new_seg.header().syn;  
        // 获得待装入负载
        string payload_string = _stream.read(payload_size);
        // 装入负载
        new_seg.payload() = Buffer(move(payload_string));
        // 是否要 fin = true
        // 从未发送过 fin  &&  _stream终止输入并且读完  &&  window内还可以装入一位fin
        if (!_fin_flag && _stream.eof() && _retrans_buffer_space + payload_string.size() < win_size && new_seg.length_in_sequence_space() < TCPConfig::MAX_PAYLOAD_SIZE) // 
            _fin_flag = new_seg.header().fin = true;
        // 没有数据要发送就break 如果payload字段没有数据而 fin == true 也可以发送
        if (new_seg.length_in_sequence_space() == 0)
            break;
        // 如果缓存区没有等待确认的seg 那么我们需要为这个新seg开启定时器
        if (_retransmission_buffer.empty()) {
            _retransmission_timer.passage = 0;
            _retransmission_timer.retransmission_timeout = _initial_retransmission_timeout;
        }
        // 发送新seg
        _segments_out.push(new_seg);
        // 缓存新seg
        _retransmission_buffer.push(new_seg);
        _retrans_buffer_space += new_seg.length_in_sequence_space();
        // 更新新的 abs_seqno
        _next_seqno += new_seg.length_in_sequence_space();
        // 如果已经fin 就退出循环
        if (new_seg.header().fin)
            break;
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) { 
    // DUMMY_CODE(ackno, window_size); 
    // 获得ackno的abs_seqno checkpoint取_next_seqno
    uint64_t abs_ack_seqno = unwrap(ackno, _isn, _next_seqno);
    // 如果确认号比尚未发送的seqno还大 则丢弃
    // abs_ack_seqno指向的字节之前的字节都收到了
    if (abs_ack_seqno > next_seqno_absolute())
        return;
    // 把已经收到确认的seg推出缓存
    while (!_retransmission_buffer.empty()) {
        TCPSegment front_seg = _retransmission_buffer.front();
        if (unwrap(front_seg.header().seqno, _isn, abs_ack_seqno) + front_seg.length_in_sequence_space() <= abs_ack_seqno) {
            _retrans_buffer_space -= front_seg.length_in_sequence_space(); // 更新已发送未确认字节数
            _retransmission_buffer.pop();
            // 如果有新的被确认seg timeout回归为初始值 重启定时器
            _retransmission_timer.retransmission_timeout = _initial_retransmission_timeout;
            _retransmission_timer.passage = 0;
        }
        else
            break;
    }
    // 无论队头的seg有没有收到确认 收到确认就应该将连续*超时*重发计数归零 
    _retransmission_timer.consecutive_retransmissions = 0;
    // 更新窗口大小
    _window_size = window_size;
    // 填充窗口
    fill_window();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) { 
    // DUMMY_CODE(ms_since_last_tick); 
    // 统计经过的时间
    _retransmission_timer.passage += ms_since_last_tick;
    // 如超时且还有未确认的seg 则重传
    if (_retransmission_timer.passage > _retransmission_timer.retransmission_timeout && !_retransmission_buffer.empty()) {
        _segments_out.push(_retransmission_buffer.front());     // 重新推入发送队列
        ++ _retransmission_timer.consecutive_retransmissions;   // 连续超时重传计数
        // 如果window_size > 0 那么说明超时是由网络拥堵造成的
        if (_window_size > 0)
            _retransmission_timer.retransmission_timeout *= 2;  // RTO * 2
        _retransmission_timer.passage = 0;  // 重置定时器
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _retransmission_timer.consecutive_retransmissions; }

// 发送一个空的确认seg
void TCPSender::send_empty_segment() {
    TCPSegment seg;
    seg.header().seqno = next_seqno();
    _segments_out.push(seg);
}
