// SPDX-License-Identifier: MIT
#include "native_3d.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace nds_native_3d {
namespace {
constexpr unsigned kVertices = 6144, kPolygons = 2048, kOperations = 131072;
constexpr int32_t kOne = 4096;
Matrix Identity() {
    Matrix result{};
    for (unsigned i = 0; i < 4; ++i) result[i * 5] = kOne;
    return result;
}
int32_t Signed(uint32_t x, unsigned bits) {
    const uint32_t mask = (uint32_t{1} << bits) - 1;
    const uint32_t sign = uint32_t{1} << (bits - 1);
    return static_cast<int32_t>((x & mask) ^ sign) - static_cast<int32_t>(sign);
}
int32_t Narrow(int64_t x) { return std::bit_cast<int32_t>(static_cast<uint32_t>(x)); }
Matrix Multiply(const Matrix& a, const Matrix& b) {
    Matrix out{};
    for (unsigned r = 0; r < 4; ++r) for (unsigned c = 0; c < 4; ++c) {
        uint64_t sum = 0;
        for (unsigned k = 0; k < 4; ++k)
            sum += static_cast<uint64_t>(int64_t{a[r * 4 + k]} * b[k * 4 + c]);
        out[r * 4 + c] = Narrow(std::bit_cast<int64_t>(sum) >> 12);
    }
    return out;
}
std::array<int32_t, 4> Transform(std::span<const int32_t, 4> v, const Matrix& m) {
    std::array<int32_t, 4> out{};
    for (unsigned c = 0; c < 4; ++c) {
        uint64_t sum = 0;
        for (unsigned r = 0; r < 4; ++r)
            sum += static_cast<uint64_t>(int64_t{v[r]} * m[r * 4 + c]);
        out[c] = Narrow(std::bit_cast<int64_t>(sum) >> 12);
    }
    return out;
}
std::array<uint8_t, 3> RGB(uint32_t word) {
    return {uint8_t(word & 31), uint8_t((word >> 5) & 31), uint8_t((word >> 10) & 31)};
}
int32_t Color(uint32_t c) { return c ? static_cast<int32_t>((c * 16 + 15) * 4096) : 0; }
int64_t Distance(const Vertex& v, unsigned plane) {
    return int64_t{v.clip[3]} + ((plane & 1) ? -int64_t{v.clip[plane / 2]} : int64_t{v.clip[plane / 2]});
}
Vertex Intersection(const Vertex& a, const Vertex& b, int64_t da, int64_t db, uint64_t id) {
    Vertex out{};
    // Clipping is host geometry, not a simulation of divider/cycle state.
    // Use an extended intermediate to avoid overflowing arbitrary guest matrices.
    const long double t = static_cast<long double>(da) / (static_cast<long double>(da) - db);
    auto mix = [&](int32_t x, int32_t y) {
        return static_cast<int32_t>(std::clamp<long double>(std::floor(x + (static_cast<long double>(y) - x) * t),
            std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
    };
    for (unsigned i = 0; i < 4; ++i) out.clip[i] = mix(a.clip[i], b.clip[i]);
    for (unsigned i = 0; i < 3; ++i) out.color[i] = mix(a.color[i], b.color[i]);
    for (unsigned i = 0; i < 2; ++i) out.texture[i] = static_cast<int16_t>(mix(a.texture[i], b.texture[i]));
    out.identity = id;
    out.viewport = a.viewport; out.perspective = a.perspective;
    return out;
}
struct ClippedPolygon {
    std::array<Vertex, 12> vertices{};
    unsigned count{};
    auto begin() { return vertices.begin(); }
    auto end() { return vertices.begin() + count; }
    auto begin() const { return vertices.begin(); }
    auto end() const { return vertices.begin() + count; }
    unsigned size() const { return count; }
    bool empty() const { return count == 0; }
    Vertex& operator[](unsigned i) { return vertices[i]; }
};
ClippedPolygon Clip(std::span<const Vertex> input, uint64_t& identity, bool far_clip) {
    unsigned outside = 0, common = 63;
    for (const auto& point : input) {
        unsigned code = 0;
        for (unsigned plane = 0; plane < 6; ++plane) code |= unsigned(Distance(point, plane) < 0) << plane;
        outside |= code; common &= code;
    }
    if (common || (!far_clip && (outside & (1u << 5)))) return {};
    ClippedPolygon first, second;
    auto* points = &first; auto* next = &second;
    std::copy(input.begin(), input.end(), points->vertices.begin());
    points->count = static_cast<unsigned>(input.size());
    if (!outside) return *points;
    for (unsigned plane = 0; plane < 6 && !points->empty(); ++plane) {
        if (!(outside & (1u << plane))) continue;
        next->count = 0;
        Vertex prior = (*points)[points->count - 1];
        int64_t dprior = Distance(prior, plane);
        for (const Vertex& current : *points) {
            const int64_t dcurrent = Distance(current, plane);
            if ((dprior < 0) != (dcurrent < 0)) {
                if (next->count == next->vertices.size()) return {};
                (*next)[next->count++] = Intersection(prior, current, dprior, dcurrent, identity++);
            }
            if (dcurrent >= 0) {
                if (next->count == next->vertices.size()) return {};
                (*next)[next->count++] = current;
            }
            prior = current; dprior = dcurrent;
        }
        std::swap(points, next);
    }
    return *points;
}
}

Service::Service() { Reset(); }
void Service::Reset() {
    state_ = {};
    state_.projection = state_.position = state_.direction = state_.texture = Identity();
    state_.viewport = 0xBFFF0000u;
    state_.box_result = 1;
    building_ = {}; pending_ = {}; display_ = {}; pending_swap_ = false;
    building_.vertices.reserve(kVertices); building_.polygons.reserve(kPolygons);
    operations_.clear(); operations_.reserve(32768);
    payload_.clear(); payload_.reserve(65536); consumed_ = 0;
    statistics_ = {};
    jobs_.clear(); job_bytes_ = 0;
    clip_dirty_ = true;
}
unsigned Service::Parameters(uint8_t c) {
    switch (c) {
    case 0x10: case 0x12: case 0x13: case 0x14: case 0x20: case 0x21: case 0x22:
    case 0x24: case 0x25: case 0x26: case 0x27: case 0x28: case 0x29: case 0x2A:
    case 0x2B: case 0x30: case 0x31: case 0x32: case 0x33: case 0x40: case 0x50:
    case 0x60: case 0x72: return 1;
    case 0x23: case 0x71: return 2;
    case 0x1B: case 0x1C: case 0x70: return 3;
    case 0x1A: return 9;
    case 0x17: case 0x19: return 12;
    case 0x16: case 0x18: return 16;
    case 0x34: return 32;
    default: return 0;
    }
}
void Service::Operation(uint8_t c, std::span<const uint32_t> parameters) {
    if (parameters.size() != Parameters(c)) throw std::invalid_argument("native geometry parameter count");
    if (operations_.size() >= kOperations) {
        Drain(false);
        if (operations_.size() >= kOperations) throw std::runtime_error("native geometry pending-frame job limit");
    }
    const auto offset = static_cast<uint32_t>(payload_.size());
    payload_.insert(payload_.end(), parameters.begin(), parameters.end());
    operations_.push_back({offset, c, static_cast<uint8_t>(parameters.size()), 0});
}
void Service::Direct(uint8_t c, uint32_t parameter) {
    const unsigned needed = Parameters(c);
    if (!needed) { Operation(c, {}); return; }
    if (state_.parameter_count && state_.active_command != c)
        throw std::runtime_error("interleaved incomplete native geometry command");
    state_.active_command = c;
    state_.parameters[state_.parameter_count++] = parameter;
    if (state_.parameter_count == needed) {
        Operation(c, {state_.parameters.data(), needed});
        state_.parameter_count = 0;
    }
}
void Service::Packed(uint32_t word) {
    ++statistics_.input_words;
    if (!state_.packed_count) { state_.packed_commands = word; state_.packed_count = 4; }
    else {
        const uint8_t c = static_cast<uint8_t>(state_.packed_commands);
        Direct(c, word);
        if (state_.parameter_count) return;
        state_.packed_commands >>= 8; --state_.packed_count;
    }
    while (state_.packed_count && !Parameters(static_cast<uint8_t>(state_.packed_commands))) {
        Operation(static_cast<uint8_t>(state_.packed_commands), {});
        state_.packed_commands >>= 8; --state_.packed_count;
    }
}
void Service::Packed(std::span<const uint32_t> words) { for (uint32_t word : words) Packed(word); }
Mesh Service::CompileMesh(std::span<const uint32_t> words) {
    if (words.size() > kOperations / 4) throw std::invalid_argument("native mesh command capacity");
    Service decoder;
    decoder.Packed(words);
    if (decoder.state_.packed_count || decoder.state_.parameter_count)
        throw std::invalid_argument("incomplete native mesh resource");
    Mesh mesh;
    mesh.operations = std::move(decoder.operations_);
    mesh.parameters = std::move(decoder.payload_);
    return mesh;
}
void Service::DrawMesh(const Mesh& mesh) {
    if (operations_.size() + mesh.operations.size() > kOperations) Drain(false);
    if (operations_.size() + mesh.operations.size() > kOperations)
        throw std::runtime_error("native mesh pending-frame capacity");
    const auto offset = static_cast<uint32_t>(payload_.size());
    payload_.insert(payload_.end(), mesh.parameters.begin(), mesh.parameters.end());
    for (auto op : mesh.operations) { op.offset += offset; operations_.push_back(op); }
    ++statistics_.native_packets;
}
void Service::Job(std::span<const uint32_t> words) {
    // Keep exact equality after hashing. An address or generation alone does
    // not identify a model buffer that a guest regenerates or aliases.
    if (words.empty()) return;
    if (state_.packed_count || state_.parameter_count || words.size() > 16384 ||
        operations_.size() + words.size() * 4 > kOperations) {
        Packed(words); return;
    }
    uint64_t hash = 14695981039346656037ull;
    for (uint32_t word : words) { hash ^= word; hash *= 1099511628211ull; }
    for (const auto& job : jobs_) if (job.hash == hash && job.words.size() == words.size() &&
        std::equal(words.begin(), words.end(), job.words.begin())) {
        const auto offset = static_cast<uint32_t>(payload_.size());
        payload_.insert(payload_.end(), job.parameters.begin(), job.parameters.end());
        for (auto op : job.operations) { op.offset += offset; operations_.push_back(op); }
        state_.parameters = job.last_parameters; state_.active_command = job.last_command;
        statistics_.input_words += words.size(); statistics_.reused_words += words.size();
        ++statistics_.cache_hits;
        return;
    }
    const size_t first = operations_.size(), parameters = payload_.size();
    Packed(words);
    if (state_.packed_count || state_.parameter_count) return;
    CompiledJob job;
    job.hash = hash; job.words.assign(words.begin(), words.end());
    job.parameters.assign(payload_.begin() + parameters, payload_.end());
    job.operations.assign(operations_.begin() + first, operations_.end());
    for (auto& op : job.operations) op.offset -= static_cast<uint32_t>(parameters);
    job.last_parameters = state_.parameters; job.last_command = state_.active_command;
    auto cost = [](const CompiledJob& value) {
        return (value.words.size() + value.parameters.size()) * sizeof(uint32_t) + value.operations.size() * sizeof(OperationEntry);
    };
    const size_t bytes = cost(job);
    while (!jobs_.empty() && (jobs_.size() >= 64 || job_bytes_ + bytes > 4 * 1024 * 1024)) {
        job_bytes_ -= cost(jobs_.front()); jobs_.erase(jobs_.begin());
    }
    job_bytes_ += bytes;
    jobs_.push_back(std::move(job)); ++statistics_.cache_builds;
}
Matrix Service::ClipMatrix() const {
    if (clip_dirty_) { clip_cache_ = Multiply(state_.position, state_.projection); clip_dirty_ = false; }
    return clip_cache_;
}
void Service::SetPresentation(uint16_t width, bool guest_wide_projection) {
    if (width < 256 || width > 448 || (width & 1)) throw std::invalid_argument("native geometry output width");
    state_.output_width = width; state_.guest_wide_projection = guest_wide_projection;
}
void Service::Widen(std::array<int32_t,4>& clip) const {
    // Title presentation contract, independently of the guest matrix readback:
    // widen the full viewport's clip volume and BOX_TEST together. Partial
    // viewports and already patched guest projections retain their own frusta.
    if (state_.output_width > 256 && !state_.guest_wide_projection &&
        (state_.viewport & 0x00FF00FFu) == 0x00FF0000u)
        clip[0] = static_cast<int32_t>(int64_t{clip[0]} * 256 / state_.output_width);
}

void Service::Stack(uint8_t c, uint32_t p) {
    auto& s = state_;
    uint32_t& sp = s.matrix_mode == 0 ? s.projection_sp : s.matrix_mode == 3 ? s.texture_sp : s.position_sp;
    const bool pair = s.matrix_mode == 1 || s.matrix_mode == 2;
    uint32_t address = pair ? p & 31u : 0u;
    if (c == 0x11) { address = sp; sp = (sp + 1) & (pair ? 63u : 1u); }
    if (c == 0x12) { sp = (sp - (pair ? Signed(p, 6) : 1)) & (pair ? 63u : 1u); address = sp; }
    if ((pair && address >= 31u) || (!pair && address != 0u)) s.stack_error = 1;
    const uint32_t at = pair ? address & 31u : 0u;
    const bool store = c == 0x11 || c == 0x13;
    if (pair) {
        if (store) { s.position_stack[at] = s.position; s.direction_stack[at] = s.direction; }
        else { s.position = s.position_stack[at]; s.direction = s.direction_stack[at]; }
    } else {
        Matrix& active = s.matrix_mode == 0 ? s.projection : s.texture;
        Matrix& saved = s.matrix_mode == 0 ? s.projection_stack : s.texture_stack;
        if (store) saved = active; else active = saved;
        sp &= 1u;
    }
}
void Service::MatrixOperation(uint8_t c, std::span<const uint32_t> p) {
    auto& s = state_;
    if (s.matrix_mode != 3) clip_dirty_ = true;
    if (c >= 0x11 && c <= 0x14) { Stack(c, p.empty() ? 0 : p[0]); return; }
    Matrix m = Identity();
    if (c == 0x16 || c == 0x18) {
        for (unsigned i = 0; i < 16; ++i) m[i] = std::bit_cast<int32_t>(p[i]);
    } else if (c == 0x17 || c == 0x19 || c == 0x1A) {
        const unsigned rows = c == 0x1A ? 3 : 4;
        for (unsigned r = 0; r < rows; ++r) for (unsigned col = 0; col < 3; ++col)
            m[r * 4 + col] = std::bit_cast<int32_t>(p[r * 3 + col]);
    } else if (c == 0x1B) {
        for (unsigned i = 0; i < 3; ++i) m[i * 5] = std::bit_cast<int32_t>(p[i]);
    } else if (c == 0x1C) {
        for (unsigned i = 0; i < 3; ++i) m[12 + i] = std::bit_cast<int32_t>(p[i]);
    }
    const bool load = c == 0x15 || c == 0x16 || c == 0x17;
    auto apply = [&](Matrix& target) { target = load ? m : Multiply(m, target); };
    if (s.matrix_mode == 0) apply(s.projection);
    else if (s.matrix_mode == 3) apply(s.texture);
    else {
        apply(s.position);
        if (s.matrix_mode == 2 && c != 0x1B) apply(s.direction);
    }
}
void Service::MapTexture(std::span<const int32_t, 3> source, unsigned bits) {
    auto& s = state_;
    for (unsigned c = 0; c < 2; ++c) {
        int64_t total = 0;
        for (unsigned i = 0; i < 3; ++i) total += int64_t{source[i]} * s.texture[i * 4 + c];
        s.mapped_texture[c] = static_cast<int16_t>(s.raw_texture[c] + (total >> (bits + 8)));
    }
}
void Service::Normal(uint32_t p) {
    auto& s = state_;
    std::array<int32_t, 3> raw{Signed(p, 10), Signed(p >> 10, 10), Signed(p >> 20, 10)};
    if ((s.texture_parameter >> 30) == 2) MapTexture(raw, 9);
    const auto normal = Transform(std::array<int32_t, 4>{raw[0], raw[1], raw[2], 0}, s.direction);
    std::array<int32_t, 3> color{int32_t(s.emission[0]), int32_t(s.emission[1]), int32_t(s.emission[2])};
    for (unsigned light = 0; light < 4; ++light) if (s.active_attributes & (1u << light)) {
        int64_t diffuse = 0, half = 0;
        for (unsigned i = 0; i < 3; ++i) {
            diffuse -= int64_t{s.light[light][i]} * normal[i];
            half -= int64_t{s.half_light[light][i]} * normal[i];
        }
        diffuse = std::clamp<int64_t>(diffuse >> 9, 0, 512);
        half = std::clamp<int64_t>(half >> 9, 0, 512);
        int64_t shiny = (half * half) >> 9;
        if (s.shininess_enabled) shiny = int64_t{s.shininess[std::min<int64_t>(127, shiny >> 2)]} * 2;
        for (unsigned i = 0; i < 3; ++i) {
            color[i] += static_cast<int32_t>((int64_t{s.diffuse[i]} * s.light_color[light][i] * diffuse) >> 14);
            color[i] += static_cast<int32_t>((int64_t{s.specular[i]} * s.light_color[light][i] * shiny) >> 14);
            color[i] += (s.ambient[i] * s.light_color[light][i]) >> 5;
        }
    }
    for (unsigned i = 0; i < 3; ++i) s.color[i] = Color(std::clamp(color[i], 0, 31));
}

void Service::SubmitPolygon(std::span<const Vertex> vertices) {
    auto points = Clip(vertices, state_.next_identity, (state_.active_attributes & (1u << 12)) != 0);
    if (points.size() < 3 || points.size() > 10) { ++statistics_.culled; return; }
    bool front = true;
    long double area = 0;
    for (unsigned i = 0; i < points.size(); ++i) {
        const auto& a = points[i].clip; const auto& b = points[(i + 1) % points.size()].clip;
        if (a[3] && b[3]) area += (static_cast<long double>(a[0]) * b[1] - static_cast<long double>(b[0]) * a[1]) / (static_cast<long double>(a[3]) * b[3]);
    }
    front = area >= 0;
    const unsigned face_bit = front ? 7 : 6;
    if (area != 0 && !(state_.active_attributes & (1u << face_bit))) { ++statistics_.culled; return; }
    size_t needed = 0;
    for (const auto& point : points) needed += point.output_index == UINT16_MAX;
    if (building_.polygons.size() >= kPolygons || building_.vertices.size() + needed > kVertices) {
        state_.overflow = 1; ++statistics_.overflows; return;
    }
    Polygon out{};
    out.count = static_cast<uint8_t>(points.size()); out.front = front;
    out.attributes = state_.active_attributes; out.texture = state_.texture_parameter;
    out.palette = state_.palette; out.viewport = state_.viewport;
    out.perspective = state_.projection[3] || state_.projection[7] || state_.projection[11];
    for (unsigned i = 0; i < points.size(); ++i) {
        uint16_t& index = points[i].output_index;
        if (index == UINT16_MAX) {
            const auto at = static_cast<uint16_t>(building_.vertices.size());
            index = at; building_.vertices.push_back(points[i]);
            // Only the current triangle/quad strip can reference this input
            // again. Keep its index on those four vertices, with no frame-wide
            // hash or identity-sized allocation.
            for (unsigned corner = 0; corner < state_.primitive_count; ++corner)
                if (state_.primitive[corner].identity == points[i].identity)
                    state_.primitive[corner].output_index = at;
        }
        out.indices[i] = index;
    }
    statistics_.vertices += needed; ++statistics_.polygons;
    if (points.size() != vertices.size()) ++statistics_.clipped;
    building_.polygons.push_back(out);
}
void Service::SubmitVertex() {
    auto& s = state_;
    Vertex v{};
    v.clip = Transform(std::array<int32_t, 4>{s.vertex[0], s.vertex[1], s.vertex[2], kOne}, ClipMatrix());
    Widen(v.clip);
    v.color = s.color; v.identity = s.next_identity++;
    v.viewport = s.viewport;
    v.perspective = s.projection[3] || s.projection[7] || s.projection[11];
    if ((s.texture_parameter >> 30) == 3) {
        const std::array<int32_t, 3> p{s.vertex[0], s.vertex[1], s.vertex[2]}; MapTexture(p, 12);
    }
    v.texture = s.mapped_texture;
    s.primitive[s.primitive_count++] = v;
    const unsigned count = (s.primitive_type & 1) ? 4 : 3;
    if (s.primitive_count < count) return;
    if (s.primitive_type == 2 && (s.strip_count & 1)) {
        std::array<Vertex, 3> triangle{s.primitive[1], s.primitive[0], s.primitive[2]}; SubmitPolygon(triangle);
    } else if (s.primitive_type == 3) {
        std::array<Vertex, 4> quad{s.primitive[0], s.primitive[1], s.primitive[3], s.primitive[2]}; SubmitPolygon(quad);
    } else SubmitPolygon({s.primitive.data(), count});
    ++s.strip_count;
    if (s.primitive_type == 2) {
        s.primitive[0] = s.primitive[1]; s.primitive[1] = s.primitive[2]; s.primitive_count = 2;
    } else if (s.primitive_type == 3) {
        s.primitive[0] = s.primitive[2]; s.primitive[1] = s.primitive[3]; s.primitive_count = 2;
    } else s.primitive_count = 0;
}

void Service::Execute(uint8_t c, std::span<const uint32_t> p) {
    auto& s = state_;
    ++statistics_.commands; ++statistics_.command_counts[c];
    if (c >= 0x11 && c <= 0x1C) { MatrixOperation(c, p); return; }
    switch (c) {
    case 0x10: s.matrix_mode = p[0] & 3; break;
    case 0x20: { const auto color = RGB(p[0]); for (unsigned i = 0; i < 3; ++i) s.color[i] = Color(color[i]); break; }
    case 0x21: Normal(p[0]); break;
    case 0x22:
        s.raw_texture = {static_cast<int16_t>(p[0]), static_cast<int16_t>(p[0] >> 16)};
        s.mapped_texture = s.raw_texture;
        if ((s.texture_parameter >> 30) == 1) for (unsigned c = 0; c < 2; ++c)
            s.mapped_texture[c] = static_cast<int16_t>((int64_t{s.raw_texture[0]} * s.texture[c] +
                int64_t{s.raw_texture[1]} * s.texture[4 + c] + s.texture[8 + c] + s.texture[12 + c]) >> 12);
        break;
    case 0x23: s.vertex = {static_cast<int16_t>(p[0]), static_cast<int16_t>(p[0] >> 16), static_cast<int16_t>(p[1])}; SubmitVertex(); break;
    case 0x24: for (unsigned i = 0; i < 3; ++i) s.vertex[i] = static_cast<int16_t>(Signed(p[0] >> (i * 10), 10) * 64); SubmitVertex(); break;
    case 0x25: s.vertex[0] = static_cast<int16_t>(p[0]); s.vertex[1] = static_cast<int16_t>(p[0] >> 16); SubmitVertex(); break;
    case 0x26: s.vertex[0] = static_cast<int16_t>(p[0]); s.vertex[2] = static_cast<int16_t>(p[0] >> 16); SubmitVertex(); break;
    case 0x27: s.vertex[1] = static_cast<int16_t>(p[0]); s.vertex[2] = static_cast<int16_t>(p[0] >> 16); SubmitVertex(); break;
    case 0x28: for (unsigned i = 0; i < 3; ++i) s.vertex[i] = static_cast<int16_t>(s.vertex[i] + Signed(p[0] >> (i * 10), 10)); SubmitVertex(); break;
    case 0x29: s.attributes = p[0]; break;
    case 0x2A: s.texture_parameter = p[0]; break;
    case 0x2B: s.palette = p[0] & 0x1FFF; break;
    case 0x30: s.diffuse = RGB(p[0]); s.ambient = RGB(p[0] >> 16);
        if (p[0] & 0x8000) for (unsigned i = 0; i < 3; ++i) s.color[i] = Color(s.diffuse[i]);
        break;
    case 0x31: s.specular = RGB(p[0]); s.emission = RGB(p[0] >> 16); s.shininess_enabled = (p[0] >> 15) & 1; break;
    case 0x32: {
        const unsigned at = p[0] >> 30;
        const auto light = Transform(std::array<int32_t, 4>{Signed(p[0],10), Signed(p[0]>>10,10), Signed(p[0]>>20,10), 0}, s.direction);
        for (unsigned i = 0; i < 3; ++i) { s.light[at][i] = light[i]; s.half_light[at][i] = (light[i] - (i == 2 ? 512 : 0)) / 2; }
        break;
    }
    case 0x33: s.light_color[p[0] >> 30] = RGB(p[0]); break;
    case 0x34: for (unsigned i = 0; i < 128; ++i) s.shininess[i] = static_cast<uint8_t>(p[i / 4] >> ((i & 3) * 8)); break;
    case 0x40: s.primitive_type = p[0] & 3; s.primitive_count = s.strip_count = 0; s.active_attributes = s.attributes; break;
    case 0x41: break; // Hardware END does not interrupt a strip.
    case 0x50:
        std::swap(pending_, building_); pending_.swap_attributes = s.swap_attributes;
        s.swap_attributes = p[0] & 3;
        building_.vertices.clear(); building_.polygons.clear();
        building_.vertices.reserve(kVertices); building_.polygons.reserve(kPolygons);
        s.next_identity = 0; s.primitive_count = s.strip_count = 0;
        pending_swap_ = true; ++statistics_.swaps; break;
    case 0x60: s.viewport = p[0]; break;
    case 0x70: {
        std::array<Vertex, 8> box{};
        const std::array<int16_t, 3> origin{static_cast<int16_t>(p[0]),static_cast<int16_t>(p[0]>>16),static_cast<int16_t>(p[1])};
        const std::array<int16_t, 3> size{static_cast<int16_t>(p[1]>>16),static_cast<int16_t>(p[2]),static_cast<int16_t>(p[2]>>16)};
        const Matrix clip = ClipMatrix();
        for (unsigned i = 0; i < 8; ++i) {
            std::array<int32_t, 4> vertex{0,0,0,kOne};
            for (unsigned c = 0; c < 3; ++c) vertex[c] = static_cast<int16_t>(origin[c] + ((i & (1u << c)) ? size[c] : 0));
            box[i].clip = Transform(vertex, clip);
            Widen(box[i].clip);
        }
        static constexpr unsigned faces[6][4] = {{0,1,3,2},{4,5,7,6},{0,1,5,4},{2,3,7,6},{0,2,6,4},{1,3,7,5}};
        s.box_result = 0;
        for (const auto& face : faces) {
            std::array<Vertex,4> quad{box[face[0]],box[face[1]],box[face[2]],box[face[3]]};
            uint64_t id = 0;
            if (!Clip(quad, id, true).empty()) { s.box_result = 1; break; }
        }
        break;
    }
    case 0x71:
        s.vertex = {static_cast<int16_t>(p[0]),static_cast<int16_t>(p[0]>>16),static_cast<int16_t>(p[1])};
        s.position_result = Transform(std::array<int32_t,4>{s.vertex[0],s.vertex[1],s.vertex[2],kOne},ClipMatrix()); break;
    case 0x72: {
        const auto result = Transform(std::array<int32_t,4>{Signed(p[0],10),Signed(p[0]>>10,10),Signed(p[0]>>20,10),0},s.direction);
        for (unsigned i = 0; i < 3; ++i)
            s.vector_result[i] = static_cast<int16_t>(Signed(static_cast<uint32_t>(result[i]) << 3, 13));
        break;
    }
    default: break; // Undefined geometry opcodes consume no parameters.
    }
}
void Service::Drain(bool observe) {
    if (observe) ++statistics_.read_barriers;
    if (consumed_ == operations_.size() || pending_swap_) return;
    ++statistics_.jobs;
    while (consumed_ < operations_.size() && !pending_swap_) {
        const auto op = operations_[consumed_++];
        Execute(op.command, {payload_.data() + op.offset, op.count});
    }
    if (consumed_ == operations_.size()) { consumed_ = 0; operations_.clear(); payload_.clear(); }
}
void Service::Flush() { Drain(false); }
bool Service::VBlank() {
    Drain(false);
    if (!pending_swap_) return false;
    std::swap(display_, pending_);
    pending_.vertices.clear(); pending_.polygons.clear(); pending_swap_ = false;
    return true;
}
void Service::AcknowledgeStatus(uint32_t value, uint32_t mask) {
    const uint32_t status = (state_.irq_mode << 30 & ~mask) | (value & mask);
    state_.irq_mode = status >> 30;
    if (value & mask & 0x8000) { state_.stack_error = 0; state_.projection_sp = state_.texture_sp = 0; }
}
void Service::AcknowledgeDisplay(uint32_t value) {
    if (value & (1u << 13)) state_.overflow = 0;
}
bool Service::IRQAsserted() const {
    const size_t queued = operations_.size() - consumed_;
    return (state_.irq_mode == 1 && queued < 128) ||
           (state_.irq_mode == 2 && queued == 0 && state_.parameter_count == 0);
}
uint32_t Service::Read(uint32_t address) {
    Drain(true);
    if (address == 0x04000060) return state_.overflow << 13;
    if (address == 0x04000600) {
        const size_t queued = operations_.size() - consumed_;
        const bool tests = std::any_of(operations_.begin() + consumed_, operations_.end(),
            [](const auto& op) { return op.command >= 0x70 && op.command <= 0x72; });
        return uint32_t(tests) | (state_.box_result << 1) | ((state_.position_sp & 31) << 8) |
            ((state_.projection_sp & 1) << 13) | (state_.stack_error << 15) |
            (static_cast<uint32_t>(std::min<size_t>(256, queued)) << 16) |
            (queued < 128 ? 1u << 25 : 0u) | (queued == 0 ? 1u << 26 : 0u) |
            (pending_swap_ ? 1u << 27 : 0u) | (state_.irq_mode << 30);
    }
    if (address == 0x04000604) {
        const Frame& frame = pending_swap_ ? pending_ : building_;
        return static_cast<uint32_t>(frame.polygons.size() | (frame.vertices.size() << 16));
    }
    if (address >= 0x04000620 && address < 0x04000630) return static_cast<uint32_t>(state_.position_result[(address - 0x04000620) / 4]);
    if (address >= 0x04000630 && address < 0x04000636) {
        const unsigned at = (address - 0x04000630) / 2;
        return uint16_t(state_.vector_result[at]) | (at < 2 ? uint32_t(uint16_t(state_.vector_result[at+1])) << 16 : 0);
    }
    if (address >= 0x04000640 && address < 0x04000680) return static_cast<uint32_t>(ClipMatrix()[(address - 0x04000640) / 4]);
    if (address >= 0x04000680 && address < 0x040006A4) {
        const unsigned at = (address - 0x04000680) / 4;
        return static_cast<uint32_t>(state_.direction[(at / 3) * 4 + at % 3]);
    }
    return 0;
}

std::vector<uint8_t> Service::Save() const {
    static_assert(std::is_trivially_copyable_v<State>);
    std::vector<uint8_t> bytes;
    auto put = [&](const auto& value) {
        const auto* p = reinterpret_cast<const uint8_t*>(&value);
        bytes.insert(bytes.end(), p, p + sizeof(value));
    };
    auto array = [&](const auto& values) {
        put(static_cast<uint32_t>(values.size()));
        const auto* p = reinterpret_cast<const uint8_t*>(values.data());
        if (!values.empty()) bytes.insert(bytes.end(), p, p + values.size() * sizeof(values[0]));
    };
    auto frame = [&](const Frame& value) { put(value.swap_attributes); array(value.vertices); array(value.polygons); };
    put(uint32_t{0x3147334E}); put(state_); put(uint32_t{pending_swap_}); put(static_cast<uint32_t>(consumed_));
    frame(building_); frame(pending_); frame(display_); array(operations_); array(payload_);
    return bytes;
}
bool Service::Load(std::span<const uint8_t> bytes, std::string* error) {
    Service restored;
    size_t offset = 0;
    auto get = [&](auto& value) {
        if (sizeof(value) > bytes.size() - offset) throw std::runtime_error("truncated native geometry state");
        std::memcpy(&value, bytes.data() + offset, sizeof(value)); offset += sizeof(value);
    };
    auto array = [&](auto& values, uint32_t limit) {
        uint32_t size; get(size);
        if (size > limit || uint64_t{size} * sizeof(values[0]) > bytes.size() - offset)
            throw std::runtime_error("invalid native geometry state count");
        values.resize(size);
        if (size) std::memcpy(values.data(), bytes.data() + offset, size * sizeof(values[0]));
        offset += size * sizeof(values[0]);
    };
    auto frame = [&](Frame& value) {
        get(value.swap_attributes); array(value.vertices,kVertices); array(value.polygons,kPolygons);
        for (const auto& p : value.polygons) {
            if (p.count < 3 || p.count > 10) throw std::runtime_error("invalid native polygon size");
            for (unsigned i = 0; i < p.count; ++i) if (p.indices[i] >= value.vertices.size())
                throw std::runtime_error("invalid native polygon vertex");
        }
    };
    try {
        uint32_t magic, pending, consumed;
        get(magic); if (magic != 0x3147334E) throw std::runtime_error("native geometry state version mismatch");
        get(restored.state_); get(pending); get(consumed);
        frame(restored.building_); frame(restored.pending_); frame(restored.display_);
        array(restored.operations_,kOperations); array(restored.payload_,kOperations*32);
        auto& s = restored.state_;
        if (offset != bytes.size() || pending > 1 || consumed > restored.operations_.size() ||
            s.matrix_mode > 3 || s.primitive_type > 3 || s.primitive_count > 3 ||
            s.packed_count > 4 || s.parameter_count > 31 || s.irq_mode > 3)
            throw std::runtime_error("invalid native geometry state fields");
        for (const auto& op : restored.operations_)
            if (op.count != Parameters(op.command) || uint64_t{op.offset}+op.count > restored.payload_.size())
                throw std::runtime_error("invalid native geometry operation span");
        if (s.parameter_count && (s.active_command > 255 || s.parameter_count >= Parameters(static_cast<uint8_t>(s.active_command))))
            throw std::runtime_error("invalid native geometry partial command");
        if (s.primitive_count >= ((s.primitive_type & 1) ? 4u : 3u))
            throw std::runtime_error("invalid native partial primitive size");
        for (unsigned i = 0; i < s.primitive_count; ++i)
            if (s.primitive[i].identity >= s.next_identity ||
                (s.primitive[i].output_index != UINT16_MAX &&
                 (s.primitive[i].output_index >= restored.building_.vertices.size() ||
                  restored.building_.vertices[s.primitive[i].output_index].identity != s.primitive[i].identity)))
                throw std::runtime_error("invalid native partial primitive identity");
        restored.pending_swap_ = pending != 0; restored.consumed_ = consumed;
        if (s.output_width < 256 || s.output_width > 448 || (s.output_width & 1) ||
            s.guest_wide_projection > 1 || s.box_result > 1 || s.stack_error > 1 ||
            s.position_sp > 63 || s.projection_sp > 1 || s.texture_sp > 1)
            throw std::runtime_error("invalid native geometry presentation/status state");
        restored.clip_dirty_ = true;
        *this = std::move(restored);
        return true;
    } catch (const std::exception& e) { if (error) *error = e.what(); return false; }
}
} // namespace nds_native_3d
