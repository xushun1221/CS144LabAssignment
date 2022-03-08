#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`
// template <typename... Targs>
// void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

int StreamReassembler::merge_substring(uint64_t l_index, std::string& l_sub, uint64_t r_index, std::string& r_sub) {
    size_t l_len = l_sub.length(), r_len = r_sub.length();
    if (l_index + l_len - 1 < r_index) // 无重合部分
        return -1;
    if (l_index + l_len >= r_index + r_len) // 完全重合
        return r_len;
    for (size_t i = l_index + l_len; i <= r_index + r_len - 1; ++ i) // 部分重合
        l_sub += r_sub[i - r_index];
    return l_index + l_len - r_index;
}

StreamReassembler::StreamReassembler(const size_t capacity) : _output(capacity), _capacity(capacity) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    //DUMMY_CODE(data, index, eof);
    if (index >= _index_of_stream + _capacity) // 超出窗口范围，丢弃
        return;
    if (index > _index_of_stream) { // 需放入map
        size_t len = data.length() < _index_of_stream + _capacity -index ? data.length() : _index_of_stream + _capacity - index;
        if (_substrings.find(index) -> second.length() < len) {
            _substrings[index] = data.substr(0, len);
            _unassembled_bytes += len;
        }
    }
    if (index <= _index_of_stream) {
        if (data.length() <= _index_of_stream - index) { // 重复的seg，丢弃
            if (eof)
                _eof = true;
            if (_eof && empty())
                _output.end_input();
            return;
        }
        size_t len = index + data.length() - _index_of_stream;
        _output.write(data.substr(data.length() - len, len)); // 刚好写入流
        _index_of_stream += len;
    }
    // merge substrings
    auto it = _substrings.begin();
    auto it_post = _substrings.begin(); ++ it_post;
    while (it_post != _substrings.end()) {
        int merged_len = merge_substring(it -> first, it -> second, it_post -> first, it_post -> second);
        if (merged_len > 0) {
            _unassembled_bytes -= merged_len;
            _substrings.erase(it_post ++);
            continue;
        }
        ++ it;
        ++ it_post;
    }
    // push bytes to ByteStream
    auto iter = _substrings.begin();
    while (iter != _substrings.end()) {
        if (iter -> first < _index_of_stream) {
            if (iter -> second.length() > _index_of_stream - iter -> first){
                size_t len = iter -> first + iter -> second.length() - _index_of_stream;
                _output.write(iter -> second.substr(iter -> second.length() - len, len));
                _index_of_stream += len;
            }
            _unassembled_bytes -= iter -> second.length();
            _substrings.erase(iter ++);
        }
        if (iter -> first == _index_of_stream) {
            _unassembled_bytes -= iter -> second.length();
            _output.write(iter -> second);
            _index_of_stream += iter -> second.length();
            _substrings.erase(iter ++);
        }
        if (iter -> first > _index_of_stream)
            break;
    }
    if (eof)
        _eof = true;
    if (_eof && empty())
        _output.end_input();
}

size_t StreamReassembler::unassembled_bytes() const { return _unassembled_bytes; }

bool StreamReassembler::empty() const { return _unassembled_bytes == 0; }
