#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

using namespace std;

ByteStream::ByteStream(const size_t capacity) : 
    _capacity(capacity),
    _written(0),
    _popped(0),
    _buffer(BufferList()),
    _input_ended(false){}

size_t ByteStream::write(const string &data) {
    if (this->eof()){
        this->set_error();
        return 0;
    }

    Buffer buffer;
    if (data.size() + this->buffer_size() > this->_capacity){
        buffer = Buffer(data.substr(0, this->_capacity - this->buffer_size()));
    }else{
        buffer = Buffer(string(data));
    }

    this->_buffer.append(BufferList(buffer));
    this->_written += buffer.size();

    return buffer.size();
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    auto size = min<size_t>(len, this->_buffer.size());

    std::string s = this->_buffer.concatenate();
    return s.substr(0, size);
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    auto size = min<size_t>(len, this->_buffer.size());

    this->_buffer.remove_prefix(size);
    this->_popped += size;
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    std::string s = this->peek_output(len);
    this->pop_output(len);
    return s;
}

void ByteStream::end_input() {
    this->_input_ended = true;
}

bool ByteStream::input_ended() const {
    return this->_input_ended;
}

size_t ByteStream::buffer_size() const {
    return this->_buffer.size();
}

bool ByteStream::buffer_empty() const {
    return this->buffer_size() == 0;
}

bool ByteStream::eof() const {
    return this->buffer_empty() && this->input_ended();
}

size_t ByteStream::bytes_written() const {
    return this->_written;
}

size_t ByteStream::bytes_read() const { 
    return this->_popped;
}

size_t ByteStream::remaining_capacity() const { 
    return this->_capacity - this->buffer_size();
}
