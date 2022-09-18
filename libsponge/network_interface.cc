#include "network_interface.hh"

#include "arp_message.hh"
#include "ethernet_frame.hh"

#include <iostream>
#include <set>

// Dummy implementation of a network interface
// Translates from {IP datagram, next hop address} to link-layer frame, and from link-layer frame to IP datagram

// For Lab 5, please replace with a real implementation that passes the
// automated checks run by `make check_lab5`.

// You will need to add private members to the class declaration in `network_interface.hh`
using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface(const EthernetAddress &ethernet_address, const Address &ip_address)
    : _ethernet_address(ethernet_address), _ip_address(ip_address) {
    cerr << "DEBUG: Network interface has Ethernet address " << to_string(_ethernet_address) << " and IP address "
         << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but may also be another host if directly connected to the same network as the destination)
//! (Note: the Address type can be converted to a uint32_t (raw 32-bit IP address) with the Address::ipv4_numeric() method.)
void NetworkInterface::send_datagram(const InternetDatagram &dgram, const Address &next_hop) {
    // convert IP address of next hop to raw 32-bit representation (used in ARP header)
    const uint32_t next_hop_ip = next_hop.ipv4_numeric();

    EthernetFrame sendingFrame;
    
    if (this->_ip_to_ether.find(next_hop_ip) == this->_ip_to_ether.cend()){
        if (this->_broadcast_timer.find(next_hop_ip) != this->_broadcast_timer.cend()){
            if (this->_broadcast_timer[next_hop_ip] < this->_broadcast_resend_time){
                this->_payload_to_send[next_hop_ip].push(dgram);
                return;
            }
        }

        ARPMessage arpMsg;
        arpMsg.opcode = ARPMessage::OPCODE_REQUEST;
        arpMsg.sender_ethernet_address = this->_ethernet_address;
        arpMsg.sender_ip_address = this->_ip_address.ipv4_numeric();
        arpMsg.target_ip_address = next_hop_ip;

        sendingFrame.header().dst = ETHERNET_BROADCAST;
        sendingFrame.header().src = this->_ethernet_address;
        sendingFrame.header().type = EthernetHeader::TYPE_ARP;
        sendingFrame.payload() = {arpMsg.serialize()};

        this->_frames_out.push(sendingFrame);

        this->_broadcast_timer[next_hop_ip] = 0;
        this->_payload_to_send[next_hop_ip] = queue<InternetDatagram>();
        this->_payload_to_send[next_hop_ip].push(dgram);

    }else{
        sendingFrame.header().dst = this->_ip_to_ether[next_hop_ip];
        sendingFrame.header().src = this->_ethernet_address;
        sendingFrame.header().type = EthernetHeader::TYPE_IPv4;
        sendingFrame.payload() = dgram.serialize();
        this->_frames_out.push(sendingFrame);
    }
}

//! \param[in] frame the incoming Ethernet frame
optional<InternetDatagram> NetworkInterface::recv_frame(const EthernetFrame &frame) {
    if (frame.header().dst != this->_ethernet_address && frame.header().dst != ETHERNET_BROADCAST){
        cerr << "A Frame sending to wrong address received " << to_string(frame.header().dst) << endl;
        return nullopt;
    }

    if (frame.header().type == EthernetHeader::TYPE_IPv4){
        InternetDatagram dGram;
        dGram.parse(frame.payload());
        return dGram;
    }else{
        ARPMessage arpMsg;
        if (arpMsg.parse(frame.payload()) != ParseResult::NoError){
            cerr << "Frame received cannot be parsed" << endl;
            return nullopt;
        }

        if (arpMsg.opcode == ARPMessage::OPCODE_REQUEST){
            this->_ip_to_ether[arpMsg.sender_ip_address] = arpMsg.sender_ethernet_address;
            this->_ip_mapping_timer[arpMsg.sender_ip_address] = 0;
            
            if (arpMsg.target_ip_address != this->_ip_address.ipv4_numeric()){
                return nullopt;
            }

            EthernetFrame replyFrame;
            ARPMessage replyMsg;

            replyMsg.opcode = ARPMessage::OPCODE_REPLY;
            replyMsg.target_ethernet_address = arpMsg.sender_ethernet_address;
            replyMsg.target_ip_address = arpMsg.sender_ip_address;
            replyMsg.sender_ethernet_address = this->_ethernet_address;
            replyMsg.sender_ip_address = this->_ip_address.ipv4_numeric();

            replyFrame.header().dst = arpMsg.sender_ethernet_address;
            replyFrame.header().src = this->_ethernet_address;
            replyFrame.header().type = EthernetHeader::TYPE_ARP;
            replyFrame.payload() = {replyMsg.serialize()};

            this->_frames_out.push(replyFrame);
        }else{
            this->_ip_to_ether[arpMsg.sender_ip_address] = arpMsg.sender_ethernet_address;
            this->_ip_mapping_timer[arpMsg.sender_ip_address] = 0;
        }

        _flush();
        return nullopt;
    }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick(const size_t ms_since_last_tick) { 
    set<uint32_t> to_delete;
    for (auto p = this->_ip_mapping_timer.begin(); p != this->_ip_mapping_timer.cend(); p ++){
        p->second += ms_since_last_tick;
        if (p->second >= this->_mapping_expiration_time){
            to_delete.insert(to_delete.end(), p->first);
        }
    }

    for (auto ip : to_delete){
        this->_ip_mapping_timer.erase(ip);
        this->_ip_to_ether.erase(ip);
    }
    
    for (auto p = this->_broadcast_timer.begin(); p != this->_broadcast_timer.cend(); p ++){
        p->second += ms_since_last_tick;
    }
}

void NetworkInterface::_flush(){
    set<uint32_t> to_delete;
    for (auto p = this->_payload_to_send.begin(); p != this->_payload_to_send.cend(); p ++){
        if (this->_ip_to_ether.find(p->first) != this->_ip_to_ether.cend()){
            Address addr = Address::from_ipv4_numeric(p->first);
            while (!p->second.empty()){
                auto payload = p->second.front();
                this->send_datagram(payload, addr);
                p->second.pop();
            }
        }
    }

    for (auto ip : to_delete){
        this->_payload_to_send.erase(ip);
        if (this->_broadcast_timer.find(ip) != this->_broadcast_timer.cend()){
            this->_broadcast_timer.erase(ip);
        }
    }
}
