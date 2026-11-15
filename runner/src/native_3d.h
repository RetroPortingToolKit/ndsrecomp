// SPDX-License-Identifier: MIT
// Host frame geometry, independently implemented from the hardware contract:
// https://problemkaputt.de/gbatek.htm#ds3dvideo
// No FIFO, DMA timing, guest cycle debt, SDL, GL or vendored geometry dependency.
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace nds_native_3d {
using Matrix = std::array<int32_t, 16>;
struct Vertex {
    std::array<int32_t, 4> clip{};
    std::array<int32_t, 3> color{}; // Nine-bit renderer colour, twelve fraction bits.
    std::array<int16_t, 2> texture{};
    uint32_t viewport{}, perspective{};
    uint64_t identity{};
    uint16_t output_index{UINT16_MAX}; // Reused only by the active primitive/strip.
};
struct Polygon {
    std::array<uint16_t, 10> indices{};
    uint32_t attributes{}, texture{}, palette{}, viewport{};
    uint8_t count{}, front{}, perspective{}, reserved{};
};
struct Frame {
    std::vector<Vertex> vertices;
    std::vector<Polygon> polygons;
    uint32_t swap_attributes{};
};
struct Statistics {
    uint64_t input_words{}, commands{}, jobs{}, vertices{}, polygons{};
    uint64_t read_barriers{}, swaps{}, clipped{}, culled{}, overflows{};
    uint64_t cache_hits{}, cache_builds{}, reused_words{};
    std::array<uint64_t, 256> command_counts{};
};

class Service {
public:
    Service();
    void Reset();
    void Packed(uint32_t word);
    void Packed(std::span<const uint32_t> words);
    // Complete display lists are lowered once to immutable native operations.
    // Partial packed commands retain ordinary ingress and their continuation.
    void Job(std::span<const uint32_t> words);
    void Direct(uint8_t command, uint32_t parameter);
    void Flush();
    // A real observation boundary: publish the pending draw job at VBlank.
    // Returns true only when a submitted frame replaced the displayed frame.
    bool VBlank();
    uint32_t Read(uint32_t address);
    void AcknowledgeStatus(uint32_t value, uint32_t mask = ~0u);
    void SetPresentation(uint16_t width, bool guest_wide_projection);
    bool IRQAsserted() const;
    const Frame& Display() const { return display_; }
    const Frame& Building() const { return building_; }
    bool PendingSwap() const { return pending_swap_; }
    const Statistics& Counters() const { return statistics_; }
    static unsigned Parameters(uint8_t command);

    // Native model jobs can supply an already decoded operation stream; they
    // share the same operation contract and never rebuild packed GX words.
    void Operation(uint8_t command, std::span<const uint32_t> parameters);
    std::vector<uint8_t> Save() const;
    bool Load(std::span<const uint8_t> bytes, std::string* error);

private:
    struct OperationEntry { uint32_t offset{}; uint8_t command{}, count{}; uint16_t reserved{}; };
    struct CompiledJob {
        uint64_t hash{};
        std::vector<uint32_t> words, parameters;
        std::vector<OperationEntry> operations;
        std::array<uint32_t, 32> last_parameters{};
        uint32_t last_command{};
    };
    struct State {
        Matrix projection{}, position{}, direction{}, texture{};
        std::array<Matrix, 32> position_stack{}, direction_stack{};
        Matrix projection_stack{}, texture_stack{};
        std::array<int16_t, 3> vertex{};
        std::array<int32_t, 3> color{};
        std::array<int16_t, 2> raw_texture{}, mapped_texture{};
        std::array<std::array<int32_t, 3>, 4> light{}, half_light{};
        std::array<std::array<uint8_t, 3>, 4> light_color{};
        std::array<uint8_t, 3> diffuse{}, ambient{}, specular{}, emission{};
        std::array<uint8_t, 128> shininess{};
        std::array<int32_t, 4> position_result{};
        std::array<int16_t, 3> vector_result{};
        std::array<Vertex, 4> primitive{};
        std::array<uint32_t, 32> parameters{};
        uint64_t next_identity{};
        uint32_t matrix_mode{}, position_sp{}, projection_sp{}, texture_sp{};
        uint32_t attributes{}, active_attributes{}, texture_parameter{}, palette{};
        uint32_t viewport{}, primitive_type{}, primitive_count{}, strip_count{};
        uint32_t stack_error{}, overflow{}, box_result{}, shininess_enabled{};
        uint32_t packed_commands{}, packed_count{}, active_command{}, parameter_count{};
        uint32_t irq_mode{}, swap_attributes{};
        uint32_t output_width{256}, guest_wide_projection{};
    } state_{};
    Frame building_, pending_, display_;
    bool pending_swap_{};
    std::vector<OperationEntry> operations_;
    std::vector<uint32_t> payload_;
    size_t consumed_{};
    Statistics statistics_{};
    std::vector<CompiledJob> jobs_;
    size_t job_bytes_{};
    mutable Matrix clip_cache_{};
    mutable bool clip_dirty_{true};

    void Execute(uint8_t command, std::span<const uint32_t> parameters);
    void Drain(bool observe);
    void MatrixOperation(uint8_t command, std::span<const uint32_t> parameters);
    void Stack(uint8_t command, uint32_t parameter);
    void Normal(uint32_t parameter);
    void SubmitVertex();
    void SubmitPolygon(std::span<const Vertex> vertices);
    void MapTexture(std::span<const int32_t, 3> source, unsigned fractional_bits);
    Matrix ClipMatrix() const;
    void Widen(std::array<int32_t, 4>& clip) const;
};
} // namespace nds_native_3d
