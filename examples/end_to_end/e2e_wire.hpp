// e2e_wire.hpp — the self-describing wire format shared by the end-to-end pipeline
// (stages 2 and 3). Unlike the minimal shm_to_network header, this one carries the
// ROS topic name + full type name in every frame, so the far side can re-publish the
// exact same ROS 2 message with NO out-of-band configuration.
//
// One frame is split into UDP fragments. Every fragment starts with this fixed
// 40-byte header; fragment 0 ALSO carries a variable-length metadata block
// ("<topic>\0<type_name>\0") immediately after the header, before the payload bytes.
// Subsequent fragments carry payload only.
#pragma once
#include <cstdint>

#pragma pack(push, 1)
struct E2EHdr {
    uint32_t magic;       // 0x45324531 = "E2E1"
    uint32_t seq;         // frame sequence (per stream)
    uint32_t frag;        // fragment index
    uint32_t nfrags;      // total fragments for this frame
    uint32_t total_len;   // total PAYLOAD bytes (CDR message size) for the frame
    uint32_t meta_len;    // length of the metadata block (only in frag 0, else 0)
    uint32_t stream_id;   // small per-stream id so the receiver can demux streams
    uint32_t encoding;    // 0=FLAT, 1=CDR (the pipeline uses CDR)
    uint32_t width;       // shape (carried through for FLAT; 0 for CDR)
    uint32_t height;
};
#pragma pack(pop)
static_assert(sizeof(E2EHdr) == 40, "E2EHdr must be 40 bytes");

static constexpr uint32_t E2E_MAGIC = 0x45324531u;
static constexpr unsigned long E2E_MTU_PAYLOAD = 1400;  // safe UDP payload
