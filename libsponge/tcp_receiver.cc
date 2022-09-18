#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    bool syn = seg.header().syn;
    bool fin = seg.header().fin;
    auto seqno = seg.header().seqno;
    string payload = seg.payload().copy();

    if (syn && !this->_ISN.has_value()){
        this->_ISN = optional<WrappingInt32>(seg.header().seqno);

        seqno = WrappingInt32(seqno.raw_value() + 1);
    }

    if (!this->_ISN.has_value()) return;

    uint64_t index = unwrap(seqno, this->_ISN.value(), this->written_bytes()) - 1;

    this->_reassembler.push_substring(payload, index, fin);
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    if (!this->_ISN.has_value()) return nullopt;
    
    auto acknumber = wrap(this->written_bytes() + 1 + this->stream_out().input_ended()
                            , this->_ISN.value());

    return optional<WrappingInt32>(WrappingInt32(acknumber));
}