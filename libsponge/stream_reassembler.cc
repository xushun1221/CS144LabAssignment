#include "stream_reassembler.hh"

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity)
    : _unassembled_chars(capacity, '\0'), // 初始化为空字符
    _unassembled_flags(capacity, false),  // 初始化为false
    _output(capacity), 
    _capacity(capacity) {}

void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    // 我们在_unassembled_chars中可以容纳的字节数，不能多于ByteStream中剩余缓冲区的容量
    // 如果当前串的起始位置，超过容纳范围，直接丢弃该串
    if (index >= _next_assemble_index + _output.remaining_capacity())
        return;
    // 如果当前串所有字节，都已经被写入ByteStream，则丢弃该串
    if (index + data.size() <= _next_assemble_index) {
        if (eof && empty()) // 如果eof标记为true，表明后续已无字节需要写入，关闭ByteStream输入
            _output.end_input();
        return;
    }
    // 只有当前串的最后一个字节落在_unassembled_chars的范围内，且eof==true时，才将_eof_flag置为true
    // 标记当前串的最后一个字节是否在_unassembled_chars的范围内
    bool flag_in_bounds = true;
    if (index + data.size() > _next_assemble_index + _output.remaining_capacity())
        flag_in_bounds = false;
    // 遍历当前串，对在_unassembled_chars范围内的字节进行判断
    for (size_t i = index; i < index + data.size() && i < _next_assemble_index + _output.remaining_capacity(); ++ i) {
        if (i >= _next_assemble_index) {
            if (_unassembled_flags[i - _next_assemble_index] == false) { // 如果该位置为空，则写入字节
                _unassembled_chars[i - _next_assemble_index] = data[i - index];
                _unassembled_flags[i - _next_assemble_index] = true;
                ++ _unassembled_bytes;
            }
        }
    }
    // 把_unassembled_chars中开头的字节写入ByteStream
    // 因为整个_unassembled_chars的大小不大于ByteStream缓冲区的剩余容量，所以不用担心写入的字节被丢弃
    string write_string{};
    for (size_t i = _next_assemble_index; _unassembled_flags[i - _next_assemble_index] == true; ++ i) {
        write_string += _unassembled_chars[i - _next_assemble_index];
        _unassembled_chars.pop_front();
        _unassembled_chars.push_back('\0');
        _unassembled_flags.pop_front();
        _unassembled_flags.push_back(false);
        ++ _next_assemble_index;
        -- _unassembled_bytes;
    }
    _output.write(write_string);
    // 判断是否需要置位_eof_flag以及是否需要关闭ByteStream输入
    if (flag_in_bounds && eof)
        _eof_flag = true;
    if (_eof_flag && empty())
        _output.end_input();
}

size_t StreamReassembler::unassembled_bytes() const { return _unassembled_bytes; }

bool StreamReassembler::empty() const { return _unassembled_bytes == 0; }