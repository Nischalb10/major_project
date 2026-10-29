#pragma once

#include <vector>
#include <random>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <cstring>
#include <fstream>
#include <algorithm>

class Hamming74 {
public:
    static uint8_t encodeNibble(uint8_t d) {
        uint8_t d0 = (d >> 0) & 1; uint8_t d1 = (d >> 1) & 1;
        uint8_t d2 = (d >> 2) & 1; uint8_t d3 = (d >> 3) & 1;
        uint8_t p0 = d0 ^ d1 ^ d3; uint8_t p1 = d0 ^ d2 ^ d3; uint8_t p2 = d1 ^ d2 ^ d3;
        return (d3 << 6) | (d2 << 5) | (d1 << 4) | (p2 << 3) | (d0 << 2) | (p1 << 1) | p0;
    }

    static std::pair<uint8_t, int> decodeNibbleWithStats(uint8_t code) {
        uint8_t p0 = (code >> 0) & 1; uint8_t p1 = (code >> 1) & 1; uint8_t d0 = (code >> 2) & 1;
        uint8_t p2 = (code >> 3) & 1; uint8_t d1 = (code >> 4) & 1; uint8_t d2 = (code >> 5) & 1; uint8_t d3 = (code >> 6) & 1;
        uint8_t s0 = p0 ^ d0 ^ d1 ^ d3; uint8_t s1 = p1 ^ d0 ^ d2 ^ d3; uint8_t s2 = p2 ^ d1 ^ d2 ^ d3;
        uint8_t syndrome = (s2 << 2) | (s1 << 1) | s0;

        int corrected = (syndrome == 0) ? 0 : 1; 
        
        if (syndrome == 1) p0 ^= 1; else if (syndrome == 2) p1 ^= 1; else if (syndrome == 3) d0 ^= 1;
        else if (syndrome == 4) p2 ^= 1; else if (syndrome == 5) d1 ^= 1; else if (syndrome == 6) d2 ^= 1;
        else if (syndrome == 7) d3 ^= 1;

        return {(d3 << 3) | (d2 << 2) | (d1 << 1) | d0, corrected};
    }

    static std::vector<uint8_t> encode(const uint8_t* data, size_t len) {
        std::vector<uint8_t> encoded(len * 2);
        for(size_t i=0; i<len; i++) {
            encoded[2*i] = encodeNibble(data[i] & 0x0F);
            encoded[2*i+1] = encodeNibble((data[i] >> 4) & 0x0F);
        }
        return encoded;
    }

    static std::pair<std::vector<uint8_t>, int> decodeWithStats(const std::vector<uint8_t>& encoded) {
        std::vector<uint8_t> decoded(encoded.size() / 2);
        int total_corrections = 0;
        for(size_t i=0; i<decoded.size(); i++) {
            auto low = decodeNibbleWithStats(encoded[2*i]);
            auto high = decodeNibbleWithStats(encoded[2*i+1]);
            decoded[i] = (high.first << 4) | low.first;
            total_corrections += low.second + high.second;
        }
        return {decoded, total_corrections};
    }
};

class ChannelModel {
private:
    static void fixHeaders(uint8_t* pdu, int length_diff) {
        uint32_t old_rls_len = (pdu[22] << 24) | (pdu[23] << 16) | (pdu[24] << 8) | pdu[25];
        uint32_t new_rls_len = old_rls_len + length_diff;
        pdu[22] = (new_rls_len >> 24) & 0xFF; pdu[23] = (new_rls_len >> 16) & 0xFF;
        pdu[24] = (new_rls_len >> 8) & 0xFF; pdu[25] = new_rls_len & 0xFF;

        uint8_t* ip_header = pdu + 26;
        if ((ip_header[0] >> 4) == 4) { 
            uint16_t old_ip_len = (ip_header[2] << 8) | ip_header[3];
            uint16_t new_ip_len = old_ip_len + length_diff;
            ip_header[2] = (new_ip_len >> 8) & 0xFF; ip_header[3] = new_ip_len & 0xFF;

            ip_header[10] = 0; ip_header[11] = 0;
            uint8_t ihl = (ip_header[0] & 0x0F) * 4;
            uint32_t sum = 0;
            for (int i = 0; i < ihl; i += 2) {
                sum += (ip_header[i] << 8) | ip_header[i+1];
            }
            while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
            uint16_t checksum = ~sum;
            ip_header[10] = (checksum >> 8) & 0xFF; ip_header[11] = checksum & 0xFF;

            if (ip_header[9] == 17) {
                uint8_t* udp = ip_header + ihl;
                uint16_t old_udp_len = (udp[4] << 8) | udp[5];
                uint16_t new_udp_len = old_udp_len + length_diff;
                udp[4] = (new_udp_len >> 8) & 0xFF; udp[5] = new_udp_len & 0xFF;
                udp[6] = 0; udp[7] = 0; 
            }
        }
    }

public:
    static void simulateUncoded(uint8_t* pdu, size_t length, double bitErrorRate) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_real_distribution<> dis(0.0, 1.0);
        static std::vector<int> history;

        std::string payload(reinterpret_cast<char*>(pdu), length);
        size_t pos = payload.find("5G_TEST_UNCODED");
        if (pos == std::string::npos) return;

        size_t text_pos = pos + 15;
        if (text_pos >= length) return;

        std::ofstream out("/tmp/5g_uncoded_tx.bin", std::ios::binary);
        out.write(reinterpret_cast<const char*>(pdu + text_pos), length - text_pos);
        out.close();

        int uncoded_errors = 0;
        for (size_t i = text_pos; i < length; ++i) {
            for (int bit = 0; bit < 8; ++bit) {
                if (dis(gen) < bitErrorRate) {
                    pdu[i] ^= (1 << bit);
                    uncoded_errors++;
                }
            }
        }

        history.push_back(uncoded_errors);
        if (history.size() > 10) history.erase(history.begin());
        double avg = 0; for(int e : history) avg += e; avg /= history.size();

        std::cout << "\n\n";
        std::cout << "  [RADIO TRANSMITTER] BASELINE UNCODED STATS\n";
        std::cout << " --------------------------------------------\n";
        std::cout << "   Current Packet Errors : " << std::setw(6) << uncoded_errors << " bits     \n";
        std::cout << "   Rolling Avg (Last 10) : " << std::fixed << std::setprecision(1) << std::setw(6) << avg << " bits / packet\n";
        std::cout << " --------------------------------------------\n";
    }

    static void simulateUncodedRX(uint8_t* pdu, size_t length) {
        std::string payload(reinterpret_cast<char*>(pdu), length);
        size_t pos = payload.find("5G_TEST_UNCODED");
        if (pos == std::string::npos) return;
        size_t text_pos = pos + 15;
        if (text_pos >= length) return;

        std::ifstream in("/tmp/5g_uncoded_tx.bin", std::ios::binary);
        if (!in.is_open()) return;
        std::vector<uint8_t> expected((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

        int residual_bits = 0;
        int residual_bytes = 0;
        size_t cmp_len = std::min(expected.size(), length - text_pos);
        for (size_t i = 0; i < cmp_len; i++) {
            uint8_t rx_byte = pdu[text_pos + i];
            uint8_t tx_byte = expected[i];
            if (rx_byte != tx_byte) {
                residual_bytes++;
                for (int b = 0; b < 8; b++) {
                    if ((rx_byte & (1 << b)) != (tx_byte & (1 << b))) residual_bits++;
                }
            }
        }

        std::cout << "\n\n";
        std::cout << "  [RADIO RECEIVER] UNCODED RESIDUAL DAMAGE\n";
        std::cout << " ------------------------------------------\n";
        std::cout << "   Unrecoverable Bit Errors : " << std::setw(4) << residual_bits << " bits      \n";
        std::cout << "   Corrupted Bytes (Chars)  : " << std::setw(4) << residual_bytes << " bytes     \n";
        std::cout << " ------------------------------------------\n";
    }

    static std::vector<uint8_t> simulateHammingTX(uint8_t* pdu, size_t length, double bitErrorRate) {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_real_distribution<> dis(0.0, 1.0);
        static std::vector<int> history;

        std::string payload(reinterpret_cast<char*>(pdu), length);
        size_t pos = payload.find("5G_TEST_HAMMING");
        if (pos == std::string::npos) return std::vector<uint8_t>(pdu, pdu + length);

        size_t text_pos = pos + 15;
        if (text_pos >= length) return std::vector<uint8_t>(pdu, pdu + length);

        size_t payload_len = length - text_pos;

        std::ofstream out("/tmp/5g_hamming_tx.bin", std::ios::binary);
        out.write(reinterpret_cast<const char*>(pdu + text_pos), payload_len);
        out.close();

        std::vector<uint8_t> encoded = Hamming74::encode(pdu + text_pos, payload_len);

        std::vector<uint8_t> expanded_pdu(pdu, pdu + text_pos);
        expanded_pdu.insert(expanded_pdu.end(), encoded.begin(), encoded.end());

        fixHeaders(expanded_pdu.data(), payload_len);

        int raw_errors = 0;
        for (size_t i = text_pos; i < expanded_pdu.size(); ++i) {
            for (int bit = 0; bit < 7; ++bit) {
                if (dis(gen) < bitErrorRate) {
                    expanded_pdu[i] ^= (1 << bit);
                    raw_errors++;
                }
            }
        }

        history.push_back(raw_errors);
        if (history.size() > 10) history.erase(history.begin());
        double avg = 0; for(int e : history) avg += e; avg /= history.size();

        std::cout << "\n\n";
        std::cout << "  [RADIO TRANSMITTER] HAMMING(7,4) FEC STATS\n";
        std::cout << " --------------------------------------------\n";
        std::cout << "   Current Packet Errors : " << std::setw(6) << raw_errors << " bits     \n";
        std::cout << "   Rolling Avg (Last 10) : " << std::fixed << std::setprecision(1) << std::setw(6) << avg << " bits / packet\n";
        std::cout << " --------------------------------------------\n";

        return expanded_pdu;
    }

    static std::vector<uint8_t> simulateHammingRX(uint8_t* pdu, size_t length) {
        static std::vector<int> history;

        std::string payload(reinterpret_cast<char*>(pdu), length);
        size_t pos = payload.find("5G_TEST_HAMMING");
        if (pos == std::string::npos) return std::vector<uint8_t>(pdu, pdu + length);

        size_t text_pos = pos + 15;
        if (text_pos >= length) return std::vector<uint8_t>(pdu, pdu + length);

        std::vector<uint8_t> encoded(pdu + text_pos, pdu + length);
        auto stats_pair = Hamming74::decodeWithStats(encoded);
        std::vector<uint8_t> decoded = stats_pair.first;
        int bits_corrected = stats_pair.second;

        int residual_bits = 0;
        int residual_bytes = 0;
        int total_injected_errors = 0;
        double success_rate = 0.0;

        std::ifstream in("/tmp/5g_hamming_tx.bin", std::ios::binary);
        if (in.is_open()) {
            std::vector<uint8_t> expected((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            
            std::vector<uint8_t> perfect_encoded = Hamming74::encode(expected.data(), expected.size());
            size_t enc_cmp_len = std::min(perfect_encoded.size(), encoded.size());
            for (size_t i = 0; i < enc_cmp_len; i++) {
                uint8_t rx_byte = encoded[i];
                uint8_t tx_byte = perfect_encoded[i];
                for (int b = 0; b < 8; b++) {
                    if ((rx_byte & (1 << b)) != (tx_byte & (1 << b))) total_injected_errors++;
                }
            }

            size_t cmp_len = std::min(expected.size(), decoded.size());
            for (size_t i = 0; i < cmp_len; i++) {
                uint8_t rx_byte = decoded[i];
                uint8_t tx_byte = expected[i];
                if (rx_byte != tx_byte) {
                    residual_bytes++;
                    for (int b = 0; b < 8; b++) {
                        if ((rx_byte & (1 << b)) != (tx_byte & (1 << b))) residual_bits++;
                    }
                }
            }

            if (total_injected_errors > 0) {
                int fixed = total_injected_errors - residual_bits;
                success_rate = ((double)fixed / total_injected_errors) * 100.0;
                if (success_rate < 0) success_rate = 0.0;
            }
        }

        std::vector<uint8_t> shrunk_pdu(pdu, pdu + text_pos);
        shrunk_pdu.insert(shrunk_pdu.end(), decoded.begin(), decoded.end());

        int length_diff = -(int)decoded.size();
        fixHeaders(shrunk_pdu.data(), length_diff);

        history.push_back(bits_corrected);
        if (history.size() > 10) history.erase(history.begin());
        double avg = 0; for(int e : history) avg += e; avg /= history.size();

        std::cout << "\n\n";
        std::cout << "  [RADIO RECEIVER] HAMMING(7,4) DECODER STATS\n";
        std::cout << " ---------------------------------------------\n";
        std::cout << "   Pre-Correction Errors   : " << std::setw(6) << total_injected_errors << " bits     \n";
        std::cout << "   Attempted Corrections   : " << std::setw(6) << bits_corrected << " bits     \n";
        std::cout << "   Unrecoverable Errors    : " << std::setw(6) << residual_bits << " bits     \n";
        std::cout << "   Corrupted Bytes         : " << std::setw(6) << residual_bytes << " bytes    \n";
        std::cout << " ---------------------------------------------\n";
        std::cout << "   Correction Success      : " << std::fixed << std::setprecision(1) << std::setw(5) << success_rate << " %\n";
        std::cout << " ---------------------------------------------\n";

        return shrunk_pdu;
    }
};
