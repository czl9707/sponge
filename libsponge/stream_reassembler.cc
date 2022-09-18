#include <set>

#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

StreamReassembler::StreamReassembler(const size_t capacity) :
    _output(capacity), 
    _capacity(capacity),
    _pending(),
    _unassembled(0),
    _first_unassembled(0),
    _eof(false) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const std::string &data, const uint64_t index, const bool eof) {
    uint64_t start = index;
    uint64_t end = index + data.size();
    this->trim(start, end);

    if (eof && index + data.size() == end){   
        this->_eof = true;
    }

    string result;
    if (end <= start) result = "";
    else{
        result = data.substr(start - index, end - start);
    }

    if (result.size() > 0) {
        this->_pending.insert(std::pair<uint64_t, string>(start, result));
        this->_unassembled += result.size();
    }

    this->flush();
    this->merge();
}

void StreamReassembler::trim(uint64_t &start, uint64_t &end) {
    if (start == end) return;

    // truncate head
    if (start <= this->_first_unassembled){
        start = this->_first_unassembled;
    }
    if (end <= this->_first_unassembled){
        end = this->_first_unassembled;
    }

    // truncate tail
    const uint64_t _first_unacceptable = this->_first_unassembled + (this->_capacity - this->stream_out().buffer_size());
    if (end >= _first_unacceptable){
        end = _first_unacceptable;
    }
    if (start >= _first_unacceptable){
        start = _first_unacceptable;
    }

    // find overlapping
    for (auto p = this->_pending.begin(); p != this->_pending.cend(); p ++){
        uint64_t p_start = p->first;
        uint64_t p_end = p_start + p->second.size();

        if (p_start <= start && p_end >= end){
            end = start;
        }else if (start <= p_start && p_end <= end){
            this->_unassembled -= p->second.size();
            p->second = "";
        }else if (p_start <= start && start < p_end){
            start = p_end;
        }else if (p_start < end && end <= p_end){
            end = p_start;
        }
    }
}

void StreamReassembler::merge() {
    std::set<uint64_t> to_delete;

    for (auto p = this->_pending.begin(); p != this->_pending.cend(); p ++){
        if (p->second.size() == 0) {
            to_delete.insert(to_delete.end(), p->first);
            continue;
        }

        uint64_t end = p->first + p->second.size();
        while (this->_pending.find(end) != this->_pending.cend()){
            auto to_join = this->_pending.find(end);
            p->second += to_join->second;
            end += to_join->second.size();
            to_join->second = "";
            to_delete.insert(to_delete.end(), to_join->first);
        }
    }

    for (auto key : to_delete){
        this->_pending.erase(key);
    }
}

void StreamReassembler::flush() {
    while (this->_pending.find(_first_unassembled) != this->_pending.cend()){
        string s = this->_pending[_first_unassembled];
        uint64_t n = this->_pending[_first_unassembled].size();

        this->_output.write(s);
        this->_pending.erase(_first_unassembled);
        this->_unassembled -= n;
        this->_first_unassembled += n;
    }

    if (this->_eof && this->_unassembled == 0){
        this->_output.end_input();
    }
}