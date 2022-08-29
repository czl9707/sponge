#include <set>

#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

StreamReassembler::StreamReassembler(const size_t capacity) :
    _output(capacity), 
    _capacity(capacity),
    _unassembled(0),
    _firstUnassembled(0),
    _eof(false),
    _pending(map<uint64_t, BufferList>()) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const std::string &data, const uint64_t index, const bool eof) {
    BufferList buffer = BufferList(std::string(data));
    uint64_t start = index;
    bool is_tail_trimmed = this->trim(buffer, start);
    
    if (eof && !is_tail_trimmed){
        this->_eof = true;
    }

    if (buffer.size() > 0) {
        this->_pending.insert(std::pair<uint64_t, BufferList>(start, buffer));
        this->_unassembled += buffer.size();
    }

    this->flush();
    this->merge();
}

bool StreamReassembler::trim(BufferList &buffer, uint64_t &start) {
    bool is_tail_trimmed = false;
    if (buffer.size() == 0) return is_tail_trimmed;

    uint64_t end = start + buffer.size();

    // truncate head
    if (end <= this->_firstUnassembled){
        buffer.remove_prefix(buffer.size());
        return is_tail_trimmed;
    }else if (start < this->_firstUnassembled){
        buffer.remove_prefix(this->_firstUnassembled - start);
        start = this->_firstUnassembled;
    }

    // truncate tail
    uint64_t _firstUnacceptable = this->_firstUnassembled + (this->_capacity - this->stream_out().buffer_size());
    if (start >= _firstUnacceptable){
        buffer.remove_prefix(buffer.size());
        return is_tail_trimmed;
    }else if (end > _firstUnacceptable){
        string s = buffer.concatenate();
        buffer.remove_prefix(buffer.size());
        buffer.append(s.substr(0, _firstUnacceptable - start));
        is_tail_trimmed = true;
    }

    // find overlapping
    for (std::pair<uint64_t, BufferList> p : this->_pending){
        uint64_t p_start = p.first;
        uint64_t p_end = p_start + p.second.size();

        if (p_start <= start && p_end >= end){
            buffer.remove_prefix(buffer.size());
            return is_tail_trimmed;
        }else if (p_start >= start && p_end <= end){
            this->_unassembled -= p.second.size();
            this->_pending.at(p.first).remove_prefix(p.second.size());
        }else if (p_start <= start && start < p_end){
            buffer.remove_prefix(p_end - start);
            start = p_end;
        }else if (p_start < end && end <= p_end){
            std::string s = buffer.concatenate();
            buffer.remove_prefix(buffer.size());
            buffer.append(s.substr(0, p_start - start));
            is_tail_trimmed = true;
        }
    }

    return is_tail_trimmed;
}

void StreamReassembler::merge() {
    set<uint64_t> to_delete;

    for (pair<uint64_t, BufferList> p : this->_pending){
        if (to_delete.find(p.first) != to_delete.cend()){
            continue;
        }

        if (p.second.size() == 0){
            to_delete.insert(p.first);
            continue;
        }

        uint64_t end = p.first + p.second.size();
        while (this->_pending.find(end) != this->_pending.cend()){
            if (to_delete.find(end) != to_delete.cend()){
                continue;
            }

            to_delete.insert(end);
            this->_pending.at(p.first).append(this->_pending.at(end));
            end += this->_pending.at(end).size();
        }
    }

    for (uint64_t key : to_delete){
        this->_pending.erase(key);
    }
}

void StreamReassembler::flush() {
    while (this->_pending.find(_firstUnassembled) != this->_pending.cend()){
        string s = this->_pending[_firstUnassembled].concatenate();
        uint64_t n = this->_pending[_firstUnassembled].size();

        this->_output.write(s);
        this->_pending.erase(_firstUnassembled);
        this->_unassembled -= n;
        this->_firstUnassembled += n;
    }

    if (this->_eof && this->_unassembled == 0){
        this->_output.end_input();
    }
}