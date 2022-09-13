#include "tcp_connection.hh"
#include "tcp_state.hh"

#include <iostream>

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const {
    return this->_cfg.send_capacity - this->_sender.bytes_in_flight();
}

size_t TCPConnection::bytes_in_flight() const {
    return this->_sender.bytes_in_flight();
}

size_t TCPConnection::unassembled_bytes() const {
    return this->_receiver.unassembled_bytes(); 
}

size_t TCPConnection::time_since_last_segment_received() const {
    return this->_time_since_last_segment_received;
}

void TCPConnection::segment_received(const TCPSegment &seg) {
    // cerr << "RECEIVE "; 
    // cerr << seg.payload().size() << " WIN " << seg.header().win << " ";
    // if (seg.header().ack) cerr << seg.header().ackno.raw_value();
    // cerr << endl;

    bool need_send_ack = seg.length_in_sequence_space();
    this->_time_since_last_segment_received = 0;
    this->_receiver.segment_received(seg); 
    
    if (seg.header().rst){
        if (TCPState::state_summary(this->_sender) == TCPSenderStateSummary::CLOSED && 
        TCPState::state_summary(this->_receiver) == TCPReceiverStateSummary::LISTEN) return;

        this->_reset_connection();
        return;
    }

    if (seg.header().ack){
        this->_sender.ack_received(seg.header().ackno, seg.header().win);
    }

    if (TCPState::state_summary(this->_receiver) == TCPReceiverStateSummary::SYN_RECV && 
        TCPState::state_summary(this->_sender) == TCPSenderStateSummary::CLOSED){
        this->connect();
        return;
    }

    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::SYN_ACKED){
        this->_linger_after_streams_finish = false;
    }

    if (TCPState::state_summary(_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        TCPState::state_summary(_sender) == TCPSenderStateSummary::FIN_ACKED && 
        !_linger_after_streams_finish){
        _is_active = false;
        return;
    }

    if(need_send_ack){
        if (this->_sender.segments_out().size() == 0){
            this->_sender.send_empty_segment();
        }
        this->_flush_segs();
    }
}

bool TCPConnection::active() const {
    return this->_is_active;
}

size_t TCPConnection::write(const string &data) {
    // cerr << "WRITE " << data.size();
    // cerr << " REST " << this->_sender.stream_in().remaining_capacity() << endl;
    // cerr << endl;

    auto result = this->_sender.stream_in().write(data);
    this->_flush_segs();
    return result; 
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    this->_sender.tick(ms_since_last_tick);
    unsigned int retrans_count = this->_sender.consecutive_retransmissions();
    if (retrans_count > this->_cfg.MAX_RETX_ATTEMPTS){
        this->_send_reset();
        this->_reset_connection();
        return;
    }
    
    this->_flush_segs();
    this->_time_since_last_segment_received += ms_since_last_tick;

    if (TCPState::state_summary(this->_sender) == TCPSenderStateSummary::FIN_ACKED &&
        TCPState::state_summary(this->_receiver) == TCPReceiverStateSummary::FIN_RECV &&
        this->_linger_after_streams_finish &&
        this->_time_since_last_segment_received >= this->_cfg.rt_timeout * 10
    ){
        _is_active = false;
        this->_linger_after_streams_finish = false;
    }
}

void TCPConnection::end_input_stream() {
    this->_sender.stream_in().end_input();
    this->_flush_segs();    
}

void TCPConnection::connect() {
    this->_sender.fill_window();
    _is_active = true;
    this->_flush_segs();
}

void TCPConnection::_send_reset() {
    TCPSegment seg;
    seg.header().rst = true;
    this->_enrich_seg(seg);

    this->_segments_out.push(seg);
}

void TCPConnection::_flush_segs() {
    // cerr << "FLUSH ";

    auto state = this->state();
    if (state == TCPState::State::LISTEN || 
    state == TCPState::State::CLOSED || 
    state == TCPState::State::RESET) return;

    this->_sender.fill_window();

    while (this->_sender.segments_out().size() > 0){
        TCPSegment seg = this->_sender.segments_out().front();
        this->_sender.segments_out().pop();

        this->_enrich_seg(seg);
        this->_segments_out.push(seg);

        // cerr << seg.payload().size() << " " << seg.header().fin << seg.header().syn << seg.header().ack << endl ;
    }
}

void TCPConnection::_reset_connection(){
    this->_sender.stream_in().set_error();
    this->_receiver.stream_out().set_error();
    this->_linger_after_streams_finish = false;
    this->_is_active = false;
}

void TCPConnection::_enrich_seg(TCPSegment& seg) const {
    auto ackno = this->_receiver.ackno();
    if (ackno.has_value()){
        seg.header().ack = true;
        seg.header().ackno = ackno.value();
    }

    seg.header().win = this->_receiver.window_size();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            this->_send_reset();
            this->_reset_connection();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
