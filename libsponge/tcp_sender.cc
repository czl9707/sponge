#include <iostream>
#include "tcp_sender.hh"
#include "tcp_state.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity)
    , _timer(RetransTimer(retx_timeout)) {
    }

size_t TCPSender::bytes_in_flight() const {
    return this->_next_seqno - this->_first_unackno;
}

void TCPSender::fill_window() {
    // if the sender is not started, send the syn.
    if (this->_next_seqno == 0){
        this->_send_syn();
        return;
    }

    uint64_t last_can_sent = this->_first_notaccept + (this->_first_notaccept == this->_first_unackno ? 1 : 0);

    while (this->_next_seqno < last_can_sent){
        int64_t size = min<uint64_t>(this->_stream.buffer_size(), this->_pkg_size);
        size = min<uint64_t>(size, last_can_sent - this->_next_seqno);

        this->send_package(
            this->_stream.read(size),
            this->_next_seqno
        );

        if (this->_stream.buffer_size() == 0) break;
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    if (this->_first_unackno == 0){
        if (ackno != this->_isn + 1){
            return;
        }
        this->_first_unackno ++;
    }

    uint64_t temp = unwrap(ackno, this->_isn, this->_next_seqno);
    if (temp >= this->_first_unackno){
        this->_first_unackno = temp;
        this->_timer.reset(temp, this->_isn);

        this->_first_notaccept = this->_first_unackno + window_size;
    }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) { 
    auto state = TCPState::state_summary(*this);
    if (state == TCPSenderStateSummary::CLOSED || 
    state == TCPSenderStateSummary::ERROR) return;

    if (this->_first_notaccept == this->_first_unackno){
        this->_timer.prone(this->_segments_out, ms_since_last_tick);
    }else{
        this->_timer.timerTick(this->_segments_out, ms_since_last_tick);
    }
}

void TCPSender::_send_syn() {
    TCPSegment seg;
    seg.header().syn = true;
    seg.header().seqno = this->_isn;
    this->_segments_out.push(seg);

    this->_next_seqno ++;
    this->_timer.push(seg);
}

void TCPSender::send_empty_segment() {
    TCPSegment seg;
    seg.header().seqno = this->next_seqno();
    this->_segments_out.push(seg);
}

void TCPSender::send_package(string &&payload, uint64_t &start){
    TCPSegment seg;
    seg.payload() = Buffer(move(payload));
    seg.header().seqno = wrap(start, this->_isn);
    uint64_t last = start + seg.payload().size();

    if (this->_stream.eof() && !this->_finsent){
        if (last < this->_first_notaccept || 
        (this->_first_unackno == this->_first_notaccept && seg.payload().size() == 0)){
            seg.header().fin = true;
            last ++;
            this->_finsent = true;
        }
    }
    
    if (seg.length_in_sequence_space() == 0) return;

    if (last > this->_next_seqno){
        this->_next_seqno = last;
    }

    this->_segments_out.push(seg);
    this->_timer.push(seg);
}

RetransTimer::RetransTimer(const unsigned int retx_timeout):
_initial_retransmission_timeout(retx_timeout){
}

void RetransTimer::push(TCPSegment &seg){
    this->_waiting_segs.push(TCPSegment(seg));
}

void RetransTimer::timerTick(std::queue<TCPSegment> &segments_out, size_t ms_since_last_tick){
    if (this->_waiting_segs.size() == 0) return;

    this->_tick_accum += ms_since_last_tick;
    if (this->_tick_accum >= (this->_initial_retransmission_timeout) * pow(2, this->_retransCounter)){
        this->_tick_accum = 0;
        this->_retransCounter ++;
        
        segments_out.push(TCPSegment(this->_waiting_segs.front()));
    }
}

void RetransTimer::prone(std::queue<TCPSegment> &segments_out, size_t ms_since_last_tick){
    if (this->_waiting_segs.size() == 0) return;

    this->_tick_accum += ms_since_last_tick;
    if (this->_tick_accum >= this->_initial_retransmission_timeout){
        this->_tick_accum = 0;
        this->_retransCounter ++;
        
        segments_out.push(TCPSegment(this->_waiting_segs.front()));
    }
}

void RetransTimer::reset(uint64_t ackno, const WrappingInt32 &isn){
    bool isReset = false;

    while (true){
        if (this->_waiting_segs.size() == 0) break;

        uint64_t startno = unwrap(this->_waiting_segs.front().header().seqno, isn, ackno);
        if (startno + 
            this->_waiting_segs.front().payload().size() + 
            this->_waiting_segs.front().header().fin <= ackno){

            this->_waiting_segs.pop();

            isReset = true;
        }else{
            break;
        }
    }

    if (isReset){
        this->_retransCounter = 0;
        this->_tick_accum = 0;
    }
}