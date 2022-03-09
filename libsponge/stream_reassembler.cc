#include "stream_reassembler.hh"

#include <cassert>

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&.../* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity): _output(capacity), _capacity(capacity) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {

    auto pos_iter = _unassembled_strings.upper_bound(index);
    if (pos_iter != _unassembled_strings.begin())
        -- pos_iter;

    size_t new_idx = index;
    if (pos_iter != _unassembled_strings.end() && pos_iter -> first <= index) {
        const size_t up_idx = pos_iter -> first;
        if (index < up_idx + pos_iter -> second.size())
            new_idx = up_idx + pos_iter -> second.size();
    }
    else if (index < _next_assemble_index)
        new_idx = _next_assemble_index;


    const size_t data_start_pos = new_idx - index;
    ssize_t data_size = data.size() - data_start_pos;

    pos_iter = _unassembled_strings.lower_bound(new_idx);

    while (pos_iter != _unassembled_strings.end() && new_idx <= pos_iter -> first) {
        const size_t data_end_pos = new_idx + data_size;
        if (data_end_pos > pos_iter -> first) {
            if (data_end_pos < pos_iter->first + pos_iter->second.size()) {
                data_size = pos_iter->first - new_idx;
                break;
            }
            else {
                _unassembled_bytes -= pos_iter->second.size();
                pos_iter = _unassembled_strings.erase(pos_iter);
                continue;
            }
        }
        else
            break;
    }

    size_t first_unacceptable_idx = _next_assemble_index + _output.remaining_capacity();
    if (first_unacceptable_idx <= new_idx)
        return;

    if (data_size > 0) {
        const string new_data = data.substr(data_start_pos, data_size);
        if (new_idx == _next_assemble_index) {
            const size_t write_byte = _output.write(new_data);
            _next_assemble_index += write_byte;
            if (write_byte < new_data.size()) {
                const string data_to_store = new_data.substr(write_byte, new_data.size() - write_byte);
                _unassembled_bytes += data_to_store.size();
                _unassembled_strings[_next_assemble_index] = data_to_store;
            }
        } else {
            const string data_to_store = new_data;
            _unassembled_bytes += data_to_store.size();
            _unassembled_strings[new_idx] = data_to_store;
        }
    }

    for (auto iter = _unassembled_strings.begin(); iter != _unassembled_strings.end(); ) {
        assert(_next_assemble_index <= iter->first);
        if (iter->first == _next_assemble_index) {
            const size_t write_num = _output.write(iter->second);
            _next_assemble_index += write_num;
            if (write_num < iter->second.size()) {
                _unassembled_bytes -= write_num;
                _unassembled_strings[_next_assemble_index] = iter->second.substr(write_num, iter->second.size() - write_num);

                //_unassembled_bytes -= iter->second.size();
                _unassembled_strings.erase(iter);
                break;
            }
            _unassembled_bytes -= iter->second.size();
            iter = _unassembled_strings.erase(iter);
        }
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