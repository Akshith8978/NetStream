#include "common/protocol.hpp"
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

using namespace netstream;

void test_empty_payload() {
    std::vector<uint8_t> empty;
    auto framed = serialize_frame(empty);
    assert(framed.size() == FRAME_HEADER_SIZE);

    FrameParser parser;
    parser.append(framed.data(), framed.size());
    Frame out;
    assert(parser.next_frame(out) == ParseStatus::Ok);
    assert(out.payload.empty());
    std::cout << "[PASS] test_empty_payload\n";
}

void test_one_byte_payload() {
    std::vector<uint8_t> one = {0x42};
    auto framed = serialize_frame(one);
    assert(framed.size() == FRAME_HEADER_SIZE + 1);

    FrameParser parser;
    parser.append(framed.data(), framed.size());
    Frame out;
    assert(parser.next_frame(out) == ParseStatus::Ok);
    assert(out.payload.size() == 1);
    assert(out.payload[0] == 0x42);
    std::cout << "[PASS] test_one_byte_payload\n";
}

void test_embedded_null_bytes() {
    std::vector<uint8_t> binary_data = {'H', 'e', 0x00, 'l', 0x00, 'o', 0xFF, 0x00};
    auto framed = serialize_frame(binary_data);

    FrameParser parser;
    parser.append(framed.data(), framed.size());
    Frame out;
    assert(parser.next_frame(out) == ParseStatus::Ok);
    assert(out.payload == binary_data);
    std::cout << "[PASS] test_embedded_null_bytes\n";
}

void test_multiple_frames_in_one_buffer() {
    std::string msg1 = "First frame";
    std::string msg2 = "Second frame";
    std::string msg3 = "Third frame";

    auto f1 = serialize_frame(msg1);
    auto f2 = serialize_frame(msg2);
    auto f3 = serialize_frame(msg3);

    std::vector<uint8_t> combined;
    combined.insert(combined.end(), f1.begin(), f1.end());
    combined.insert(combined.end(), f2.begin(), f2.end());
    combined.insert(combined.end(), f3.begin(), f3.end());

    FrameParser parser;
    parser.append(combined.data(), combined.size());

    Frame out1, out2, out3, out4;
    assert(parser.next_frame(out1) == ParseStatus::Ok);
    assert(out1.as_string() == msg1);

    assert(parser.next_frame(out2) == ParseStatus::Ok);
    assert(out2.as_string() == msg2);

    assert(parser.next_frame(out3) == ParseStatus::Ok);
    assert(out3.as_string() == msg3);

    assert(parser.next_frame(out4) == ParseStatus::NeedMoreData);
    std::cout << "[PASS] test_multiple_frames_in_one_buffer\n";
}

void test_frame_split_across_multiple_writes() {
    std::string msg = "Split across multiple reads test payload";
    auto framed = serialize_frame(msg);

    FrameParser parser;
    Frame out;

    // Feed byte by byte
    for (size_t i = 0; i < framed.size() - 1; ++i) {
        parser.append(&framed[i], 1);
        assert(parser.next_frame(out) == ParseStatus::NeedMoreData);
    }

    // Append last byte
    parser.append(&framed.back(), 1);
    assert(parser.next_frame(out) == ParseStatus::Ok);
    assert(out.as_string() == msg);
    std::cout << "[PASS] test_frame_split_across_multiple_writes\n";
}

void test_oversized_payload() {
    FrameParser parser(1024); // max size 1 KB

    std::vector<uint8_t> big(2048, 'A');
    auto framed = serialize_frame(big);

    parser.append(framed.data(), framed.size());
    Frame out;
    assert(parser.next_frame(out) == ParseStatus::FrameTooLarge);
    std::cout << "[PASS] test_oversized_payload\n";
}

int main() {
    std::cout << "--- Running Protocol Unit Tests ---\n";
    test_empty_payload();
    test_one_byte_payload();
    test_embedded_null_bytes();
    test_multiple_frames_in_one_buffer();
    test_frame_split_across_multiple_writes();
    test_oversized_payload();
    std::cout << "All protocol unit tests passed successfully!\n";
    return 0;
}
