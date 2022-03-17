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
    , _retrans_timer(retx_timeout) {}

uint64_t TCPSender::bytes_in_flight() const { return _outstanding_bytes; }

void TCPSender::fill_window() {
    // 尽管接收方的窗口大小为0 还是要发送报文段以期得到接收方返回的最新窗口大小
    // 我们需要用另一个变量来表示_window_size
    // 因为如果我们将_window_size 视作 1 实际上是超出接收方的接受范围
    // 直接令其为1 在调用tick()时 无法判断超时的原因是网络问题还是接收方窗口为0
    // 会导致错误的二进制指数退避
    size_t win_size = _window_size == 0 ? 1 : _window_size;
    // 循环填充窗口 窗口大小大于以发送的字节数
    while (win_size > _outstanding_bytes) {
        // 构造新报文段
        TCPSegment segment;
        // 如果没有握手 立即发送syn信号
        if (!_syn_flag) {
            segment.header().syn = true;
            _syn_flag = true;
        }
        // 装入序列号
        segment.header().seqno = next_seqno();
        // 装入负载  注意syn会占用一个payload_size
        size_t payload_size = min(TCPConfig::MAX_PAYLOAD_SIZE, win_size - _outstanding_bytes) - segment.header().syn;
        // 从字节流中读取字节
        string payload = _stream.read(payload_size);
        // 判断是否装入fin 装入fin的条件为
        // 1.没发送过fin信号 2.字节流已经读取完毕并关闭
        // 3.接收方能接收的字节数 > 装入负载数 + syn(算一个)
        // 这样才能将fin装入seg
        // 若接收方当前能够接收的字节数 == 装入负载数 + syn
        // 那么fin信号应装在下一个空负载的seg中
        if (!_fin_flag && _stream.eof() && payload.size() + segment.header().syn < win_size - _outstanding_bytes)
            _fin_flag = segment.header().fin = true;
        // 装入负载
        segment.payload() = Buffer(move(payload));
        // 发送数据包的条件是 该数据包有占用seqno (包括 syn fin)
        if (segment.length_in_sequence_space() == 0)
            break;
        // 如果缓存区没有等待确认的seg 我们应为这个新的seg开启定时器
        if (_outstanding_queue.empty()) {
            _retrans_timer.timeout = _initial_retransmission_timeout;
            _retrans_timer.timecount = 0;
        }
        // 发送
        _segments_out.push(segment);
        // 缓存新的seg
        _outstanding_bytes += segment.length_in_sequence_space();
        _outstanding_queue.push(segment);
        // 更新 _next_seqno
        _next_seqno += segment.length_in_sequence_space();
        // 如果发送完毕立即退出fill
        if (segment.header().fin)
            break;
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    // 获得绝对ack的seqno
    size_t abs_seqno = unwrap(ackno, _isn, _next_seqno);
    // abs_seqno表示该字节之前的所有字节已收到
    // 确认号不能大于待发送字节
    if (abs_seqno > _next_seqno)
        return;
    // 把已经收到确认的seg弹出缓存区
    while (!_outstanding_queue.empty()) {
        const TCPSegment &seg = _outstanding_queue.front();
        // 如果队头seg（最先发送的）完全确认
        if (unwrap(seg.header().seqno, _isn, abs_seqno) + seg.length_in_sequence_space() <= abs_seqno) {
            // 弹出队列
            _outstanding_bytes -= seg.length_in_sequence_space();
            _outstanding_queue.pop();
            // 如果有新的数据包被成功接收 RTO回归初始值 定时器重新计时
            _retrans_timer.timeout = _initial_retransmission_timeout;
            _retrans_timer.timecount = 0;
        }
        // 如果seg没有被完全确认 说明后面的seg也一样
        else
            break;
    }
    // 只要收到了ack 则说明网络并没有中断
    // 那么连续重传计数也应该归零
    _retrans_timer.consecutive_retransmissions_count = 0;
    // 更新窗口大小
    _window_size = window_size;
    // 填充发送窗口
    fill_window();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    // 统计经过的时间
    _retrans_timer.timecount += ms_since_last_tick;
    // 如果存在未确认的seg 且 超时 则重传最先发送的seg
    if (_retrans_timer.timecount >= _retrans_timer.timeout && !_outstanding_queue.empty()) {
        // _window_size > 0 说明接收方还在接收
        // 如果超时则是网络拥堵导致
        // 启动二进制指数退避
        if (_window_size > 0)
            _retrans_timer.timeout *= 2;
        // 重新计时
        _retrans_timer.timecount = 0;
        // 重发
        _segments_out.push(_outstanding_queue.front());
        // 超时则将超时重发计数加一
        ++_retrans_timer.consecutive_retransmissions_count;
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return _retrans_timer.consecutive_retransmissions_count; }

void TCPSender::send_empty_segment() {
    TCPSegment segment;
    segment.header().seqno = next_seqno();
    _segments_out.push(segment);
}