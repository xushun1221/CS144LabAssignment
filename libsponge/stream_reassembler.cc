#include "stream_reassembler.hh"
#include <cassert>
// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`
// template <typename... Targs>
// void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity) : _output(capacity), _capacity(capacity) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    //DUMMY_CODE(data, index, eof);

    // 首先处理新传入的字符串，若字符串和ByteStream或_unassembled_strings中的数据有重叠部分
    // 则截断重叠的部分

    // 在_unassembled_strings中获取最后一个字节索引小于等于index的迭代器指针
    // 即该字符串的前面第一个串
    auto iter = _unassembled_strings.upper_bound(index);
    if (iter != _unassembled_strings.end())
        -- iter;
    // 获得当前子串的新起始位置（截断后的起始位置）
    size_t new_index = index;
    // 前面有子串，要判断是否和当前串重叠
    if (iter != _unassembled_strings.end() && iter -> first <= index) {
        const size_t front_index = iter -> first;
        // 如果当前串和前面的子串，有重叠的部分，截断当前串
        if (index < front_index + iter -> second.size())
            new_index = front_index + iter -> second.size(); // !!如果前串把当前串完全覆盖，则new_index会超出当前串的索引范围    
    }// 如果在_unassembled_strings中，没有字节索引小于当前串的子串，则考察当前串是否与Bytetream有重叠部分
    else if (index < _next_assemble_index)
        new_index = _next_assemble_index;
    // 当前串新起始位置对应的data索引
    const size_t data_start_index = new_index - index; // !!如果前串覆盖当前串，data_start_index >= data.size()
    size_t data_size = data.size() - data_start_index; // !!data_size < 0，此时size_t data_size会上溢，导致data_size变非常大，这种情况会丢弃该字符串，不会产生错误
    // 获得当前串后面一个串的迭代器指针
    iter = _unassembled_strings.lower_bound(new_index); // 当前的iter是前串指针，new_index >= index，lower_bound找的第一个大于等于index索引的指针就指向后一个子串
    // 用lower_bound是因为new_index 可能和后面子串的索引相同
    // 后面有子串，要判断是否和当前串重叠
    while (iter != _unassembled_strings.end() && new_index <= iter -> first) { // new_index 有可能和 iter -> first重合，即前串和后串首尾相连且当前串和它们都有重叠
        const size_t end_index = new_index + data_size;
        // 如果和后串有重叠
        if (end_index >= iter -> first) { // ??????? >= or >
            // 如果当前串部分覆盖后串
            if (end_index < iter -> first + iter -> second.size()) {
                data_size = iter -> first - new_index; // 把当前串与后串重叠的部分截去
                break;
            }// 如果完全覆盖后串 完全覆盖后串才会和再后面的串有机会重叠，才需要进一步循环处理
            else {
                _unassembled_bytes -= iter -> second.size(); // 删去后串
                iter = _unassembled_strings.erase(iter); // 删去后串，指针向后一个
                continue;
            }
        }
        else // 没有和后串重叠，退出循环
            break;
    }
    // 获得字节索引的上界索引 // 我们在_unassembled_strings中存储的字节数不能多于ByteStream中剩余的缓存区大小
    size_t first_unacceptable_index = _next_assemble_index + _output.remaining_capacity();
    // 如果new_index超过了这个上界，我们就丢弃这个子串
    if (first_unacceptable_index < new_index)
        return;
    // 这里有个小问题，new_index < first_unacceptable_index 但是 new_index + data_size > first_unacceptable_index
    // 这样的子串，我们仍然接受它，因为它只会造成很小的内存代价

    // 如果还有字节是之前没写入到_unassembled_strings中的
    if (data_size > 0) {
        const string new_substring = data.substr(data_start_index, data_size);
        // 如果该字符串刚好是可以写入ByteStream的，就写入
        if (new_index == _next_assemble_index) {
            const size_t written_bytes = _output.write(new_substring);
            _next_assemble_index += written_bytes;
            // 如果ByteStream的缓冲区不足，则保存到_unassembled_strings中
            if (written_bytes < new_substring.size()) {
                const string store_substring = new_substring.substr(written_bytes, new_substring.size() - written_bytes);
                _unassembled_bytes += store_substring.size();
                _unassembled_strings[_next_assemble_index] = store_substring;
            }
        }// 不是刚好可以写入ByteStream，就保存到_unassembled_strings中
        else {
            const string store_substring = new_substring;
            _unassembled_bytes += store_substring.size();
            _unassembled_strings[new_index] = store_substring;
        }
    }
    
    // 将_unassembled_strings中的子串写入到ByteStream中
    for (auto it = _unassembled_strings.begin(); it != _unassembled_strings.end(); ) {
        assert(_next_assemble_index <= it -> first);
        // 如果当前iter的内容刚好要写入ByteStream中
        if (_next_assemble_index == it -> first) {
            const size_t written_bytes = _output.write(it -> second);
            _next_assemble_index += written_bytes;
            // 没写全说明缓冲区不足，保存没写的部分，并退出循环
            if (written_bytes < it -> second.size()) {
                _unassembled_bytes -= written_bytes;
                _unassembled_strings[_next_assemble_index] = it -> second.substr(written_bytes, it -> second.size() - written_bytes);
                _unassembled_strings.erase(it);
                break;
            }
            // 写全了
            _unassembled_bytes -= it -> second.size();
            it = _unassembled_strings.erase(it);
        }// 没有要写的就退出循环
        else
            break;
    }
    if (eof)
        _eof_index = index + data.size();
    if (_eof_index <= _next_assemble_index)
        _output.end_input();
}

size_t StreamReassembler::unassembled_bytes() const { return _unassembled_bytes; }

bool StreamReassembler::empty() const { return _unassembled_bytes == 0; }
